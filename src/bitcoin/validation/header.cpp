// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/header.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace bitcoin {
namespace {

class pow_number
{
public:
    constexpr pow_number() noexcept = default;

    [[nodiscard]] static constexpr pow_number from_bytes(std::span<const std::byte, 32> bytes) noexcept
    {
        pow_number result;
        for (std::size_t limb{0}; limb < result.m_limbs.size(); ++limb) {
            std::uint32_t value{0};
            for (std::size_t byte{0}; byte < 4; ++byte) {
                value |= static_cast<std::uint32_t>(
                             std::to_integer<unsigned char>(bytes[limb * 4 + byte]))
                         << (8U * byte);
            }
            result.m_limbs[limb] = value;
        }
        return result;
    }

    [[nodiscard]] static constexpr pow_number from_compact(std::uint32_t compact) noexcept
    {
        pow_number result;
        const auto size{static_cast<std::uint8_t>(compact >> 24U)};
        std::uint32_t word{compact & 0x007fffffU};

        if (size <= 3U) {
            word >>= 8U * (3U - size);
            result.m_limbs[0] = word;
            return result;
        }

        const auto offset{static_cast<std::size_t>(size - 3U)};
        const std::size_t limb{offset / 4U};
        const unsigned shift{static_cast<unsigned>((offset % 4U) * 8U)};
        if (limb < result.m_limbs.size()) {
            result.m_limbs[limb] |= word << shift;
        }
        if (shift > 8U && limb + 1U < result.m_limbs.size()) {
            result.m_limbs[limb + 1U] |= word >> (32U - shift);
        }
        return result;
    }

    [[nodiscard]] constexpr bool zero() const noexcept
    {
        return std::ranges::all_of(m_limbs, [](std::uint32_t limb) { return limb == 0; });
    }

    [[nodiscard]] constexpr std::uint8_t byte_at(std::size_t index) const noexcept
    {
        return static_cast<std::uint8_t>((m_limbs[index / 4U] >> (8U * (index % 4U))) & 0xffU);
    }

    [[nodiscard]] constexpr std::size_t byte_size() const noexcept
    {
        for (std::size_t index{32}; index > 0; --index) {
            if (byte_at(index - 1U) != 0U) {
                return index;
            }
        }
        return 0;
    }

    [[nodiscard]] constexpr std::uint32_t compact() const noexcept
    {
        auto size{byte_size()};
        std::uint32_t compact{0};
        if (size <= 3U) {
            for (std::size_t index{0}; index < size; ++index) {
                compact |= static_cast<std::uint32_t>(byte_at(index)) << (8U * index);
            }
            compact <<= 8U * (3U - size);
        } else {
            compact =
                static_cast<std::uint32_t>(byte_at(size - 3U)) |
                (static_cast<std::uint32_t>(byte_at(size - 2U)) << 8U) |
                (static_cast<std::uint32_t>(byte_at(size - 1U)) << 16U);
        }

        if ((compact & 0x00800000U) != 0U) {
            compact >>= 8U;
            ++size;
        }
        return compact | (static_cast<std::uint32_t>(size) << 24U);
    }

    [[nodiscard]] bool multiply(std::uint64_t factor) noexcept
    {
        unsigned __int128 carry{0};
        for (auto& limb : m_limbs) {
            const auto product{static_cast<unsigned __int128>(limb) * factor + carry};
            limb = static_cast<std::uint32_t>(product);
            carry = product >> 32U;
        }
        return carry != 0;
    }

    void divide(std::uint64_t divisor) noexcept
    {
        unsigned __int128 remainder{0};
        for (std::size_t index{m_limbs.size()}; index > 0; --index) {
            const auto current{(remainder << 32U) | m_limbs[index - 1U]};
            m_limbs[index - 1U] = static_cast<std::uint32_t>(current / divisor);
            remainder = current % divisor;
        }
    }

    [[nodiscard]] constexpr std::array<std::byte, 32> bytes() const noexcept
    {
        std::array<std::byte, 32> result{};
        for (std::size_t limb{0}; limb < m_limbs.size(); ++limb) {
            const auto value{m_limbs[limb]};
            for (std::size_t byte{0}; byte < 4; ++byte) {
                result[limb * 4 + byte] = static_cast<std::byte>((value >> (8U * byte)) & 0xffU);
            }
        }
        return result;
    }

    friend constexpr auto operator<=>(const pow_number& left, const pow_number& right) noexcept
    {
        for (std::size_t index{left.m_limbs.size()}; index > 0; --index) {
            const auto left_limb{left.m_limbs[index - 1]};
            const auto right_limb{right.m_limbs[index - 1]};
            if (left_limb != right_limb) {
                return left_limb <=> right_limb;
            }
        }
        return std::strong_ordering::equal;
    }

    friend constexpr bool operator==(const pow_number&, const pow_number&) noexcept = default;

private:
    std::array<std::uint32_t, 8> m_limbs{};
};

struct target_result {
    bool valid{false};
    pow_number value;
};

[[nodiscard]] constexpr bool compact_negative(std::uint32_t compact) noexcept
{
    return (compact & 0x00800000U) != 0U && (compact & 0x007fffffU) != 0U;
}

[[nodiscard]] constexpr bool compact_overflows(std::uint32_t compact) noexcept
{
    const auto size{compact >> 24U};
    const auto word{compact & 0x007fffffU};
    return word != 0U &&
           (size > 34U ||
            (word > 0xffU && size > 33U) ||
            (word > 0xffffU && size > 32U));
}

[[nodiscard]] target_result derive_target(std::uint32_t compact, proof_of_work_limit limit) noexcept
{
    if (compact_negative(compact) || compact_overflows(compact)) {
        return {};
    }
    const auto target{pow_number::from_compact(compact)};
    if (target.zero()) {
        return {};
    }
    if (target > pow_number::from_bytes(std::span<const std::byte, 32>{limit.bytes()})) {
        return {};
    }
    return target_result{true, target};
}

[[nodiscard]] constexpr std::int64_t seconds_since_epoch(block_time time) noexcept
{
    return static_cast<std::int64_t>(time.seconds_since_epoch());
}

[[nodiscard]] constexpr bool too_far_future(block_time candidate, validation_time operation_time) noexcept
{
    return candidate.as_sys_seconds() > operation_time.value() + std::chrono::hours{2};
}

[[nodiscard]] constexpr bool not_after_mtp(block_time candidate, const median_time_past* previous_mtp) noexcept
{
    return previous_mtp != nullptr && candidate.as_sys_seconds() <= previous_mtp->value();
}

[[nodiscard]] validation_rejection reject(validation_rule_id id, static_text reason) noexcept
{
    return validation_rejection::rule(validation_rejection_code::rule_violation, id, reason);
}

[[nodiscard]] verify_result<header_facts> invalid(validation_rule_id id, static_text reason)
{
    return verify_result<header_facts>::ok(validation_decision<header_facts>::invalid(
        reject(id, reason)));
}

[[nodiscard]] operation_result<std::int64_t> difficulty_adjustment_interval(const consensus_params& params)
{
    if (params.target_spacing.count() <= 0) {
        return operation_result<std::int64_t>::failed(operation_error::make(
            operation_error_code::invariant_violation,
            "difficulty target spacing must be positive"));
    }
    if (params.target_timespan.count() <= 0) {
        return operation_result<std::int64_t>::failed(operation_error::make(
            operation_error_code::invariant_violation,
            "difficulty target timespan must be positive"));
    }

    const auto interval{params.difficulty_adjustment_interval()};
    if (interval <= 0) {
        return operation_result<std::int64_t>::failed(operation_error::make(
            operation_error_code::invariant_violation,
            "difficulty adjustment interval must be positive"));
    }
    return operation_result<std::int64_t>::ok(interval);
}

[[nodiscard]] operation_result<pow_number> stored_target(std::uint32_t bits, proof_of_work_limit limit)
{
    const auto target{derive_target(bits, limit)};
    if (!target.valid) {
        return operation_result<pow_number>::failed(operation_error::make(
            operation_error_code::malformed_stored_data,
            "stored difficulty target is malformed"));
    }
    return operation_result<pow_number>::ok(target.value);
}

[[nodiscard]] pow_number retarget(
    pow_number target,
    std::int64_t actual_timespan,
    std::int64_t target_timespan,
    pow_number limit) noexcept
{
    const auto minimum_timespan{target_timespan / 4};
    const auto maximum_timespan{target_timespan * 4};
    actual_timespan = std::clamp(actual_timespan, minimum_timespan, maximum_timespan);

    const bool overflow{target.multiply(static_cast<std::uint64_t>(actual_timespan))};
    if (overflow) {
        return limit;
    }
    target.divide(static_cast<std::uint64_t>(target_timespan));
    if (target > limit) {
        return limit;
    }
    return target;
}

[[nodiscard]] operation_result<target_bits> expected_difficulty_bits(
    const block_header& header,
    const header_context& context,
    const consensus_params& params)
{
    const auto pow_limit_bits{validation_support::compact_work_limit(params.pow_limit)};
    if (!context.has_parent) {
        if (context.height.value() != 0) {
            return operation_result<target_bits>::failed(operation_error::make(
                operation_error_code::invariant_violation,
                "non-genesis header context has no parent"));
        }
        return operation_result<target_bits>::ok(pow_limit_bits);
    }

    const auto interval{difficulty_adjustment_interval(params)};
    if (interval.has_error()) {
        return operation_result<target_bits>::failed(interval.assume_error());
    }

    const auto candidate_height{static_cast<std::int64_t>(context.height.value())};
    if (candidate_height % interval.assume_value() != 0) {
        if (params.allow_min_difficulty_blocks) {
            const auto spacing{params.target_spacing.count()};
            if (spacing > (std::numeric_limits<std::int64_t>::max() - seconds_since_epoch(context.parent_time)) / 2) {
                return operation_result<target_bits>::failed(operation_error::make(
                    operation_error_code::unsupported_operation,
                    "difficulty target spacing exceeds validation library range"));
            }
            if (seconds_since_epoch(header.time()) > seconds_since_epoch(context.parent_time) + spacing * 2) {
                return operation_result<target_bits>::ok(pow_limit_bits);
            }
            if (context.has_last_non_min_difficulty_block) {
                return operation_result<target_bits>::ok(context.last_non_min_difficulty_block_target);
            }
        }
        return operation_result<target_bits>::ok(context.parent_target);
    }

    if (params.no_retargeting) {
        return operation_result<target_bits>::ok(context.parent_target);
    }
    if (!context.has_first_difficulty_period_block) {
        return operation_result<target_bits>::failed(operation_error::make(
            operation_error_code::data_unavailable,
            "difficulty period ancestry is unavailable"));
    }

    const auto base_target{stored_target(
        params.enforce_bip94 ?
            context.first_difficulty_period_block_target.value() :
            context.parent_target.value(),
        params.pow_limit)};
    if (base_target.has_error()) {
        return operation_result<target_bits>::failed(base_target.assume_error());
    }

    const auto timespan{params.target_timespan.count()};
    if (timespan > std::numeric_limits<std::int64_t>::max() / 4) {
        return operation_result<target_bits>::failed(operation_error::make(
            operation_error_code::unsupported_operation,
            "difficulty target timespan exceeds validation library range"));
    }

    const auto actual_timespan{
        seconds_since_epoch(context.parent_time) -
        seconds_since_epoch(context.first_difficulty_period_block_time)};
    const auto pow_limit{pow_number::from_bytes(std::span<const std::byte, 32>{params.pow_limit.bytes()})};
    return operation_result<target_bits>::ok(target_bits{
        retarget(base_target.assume_value(), actual_timespan, timespan, pow_limit).compact()});
}

[[nodiscard]] operation_result<bool> violates_timewarp(
    const block_header& header,
    const header_context& context,
    const consensus_params& params)
{
    if (!params.enforce_bip94 || !context.has_parent) {
        return operation_result<bool>::ok(false);
    }

    const auto interval{difficulty_adjustment_interval(params)};
    if (interval.has_error()) {
        return operation_result<bool>::failed(interval.assume_error());
    }
    if (static_cast<std::int64_t>(context.height.value()) % interval.assume_value() != 0) {
        return operation_result<bool>::ok(false);
    }
    if (params.max_timewarp.count() < 0) {
        return operation_result<bool>::failed(operation_error::make(
            operation_error_code::invariant_violation,
            "maximum timewarp allowance must not be negative"));
    }

    return operation_result<bool>::ok(
        seconds_since_epoch(header.time()) <
        seconds_since_epoch(context.parent_time) - params.max_timewarp.count());
}

} // namespace

namespace validation_support {

target_bits compact_work_limit(proof_of_work_limit limit) noexcept
{
    return target_bits{pow_number::from_bytes(std::span<const std::byte, 32>{limit.bytes()}).compact()};
}

} // namespace validation_support

verify_result<header_facts> assess_header_with_context(
    const block_header& header,
    const header_context& context,
    validation_time time,
    const consensus_params& params)
{
    return verify_exception_boundary<header_facts>([&]() -> verify_result<header_facts> {
        if (context.has_parent) {
            if (header.previous_block_hash() != context.parent_hash) {
                return invalid(validation_rule_id::h01_previous_hash_parent, "header previous hash does not reference parent");
            }
        } else if (header.previous_block_hash() != block_hash{}) {
            return invalid(validation_rule_id::h01_previous_hash_parent, "genesis header previous hash is not null");
        }

        if (header.version() < params.minimum_block_version) {
            return invalid(validation_rule_id::h06_retired_version, "header version is below the active minimum version");
        }

        const auto target{derive_target(header.bits(), params.pow_limit)};
        if (!target.valid) {
            return invalid(validation_rule_id::h02_proof_of_work, "proof of work target is malformed");
        }

        const auto expected_bits{expected_difficulty_bits(header, context, params)};
        if (expected_bits.has_error()) {
            return verify_result<header_facts>::failed(expected_bits.assume_error());
        }
        if (header.bits() != expected_bits.assume_value().value()) {
            return invalid(validation_rule_id::h03_difficulty_transition, "header target does not match difficulty transition");
        }

        const auto timewarp{violates_timewarp(header, context, params)};
        if (timewarp.has_error()) {
            return verify_result<header_facts>::failed(timewarp.assume_error());
        }
        if (timewarp.assume_value()) {
            return invalid(validation_rule_id::h07_timewarp, "header time is too far before parent at difficulty boundary");
        }

        if (not_after_mtp(header.time(), context.has_parent ? &context.previous_median_time_past : nullptr)) {
            return invalid(validation_rule_id::h04_median_time_past, "header time is not after parent median time");
        }

        if (too_far_future(header.time(), time)) {
            return invalid(validation_rule_id::h05_future_time, "header time is too far after validation time");
        }

        if (pow_number::from_bytes(as_bytes(header.hash())) > target.value) {
            return invalid(validation_rule_id::h02_proof_of_work, "proof of work does not satisfy target");
        }

        return verify_result<header_facts>::ok(validation_decision<header_facts>::valid(
            header_facts{header.hash()}));
    },
                                                   "header validation exhausted resources", operation_error_code::internal_bug, "header validation threw an exception");
}

} // namespace bitcoin
