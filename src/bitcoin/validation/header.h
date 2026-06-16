// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_HEADER_H
#define BITCOIN_BITCOIN_VALIDATION_HEADER_H

#include <bitcoin/protocol/block_header.h>
#include <bitcoin/protocol/chain_view.h>
#include <bitcoin/validation/context.h>
#include <bitcoin/validation/result.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

namespace bitcoin {

class header_context_result;

class validated_header_context
{
public:
    [[nodiscard]] const header_context& context() const noexcept { return m_context; }

private:
    template <chain_view Chain>
    friend header_context_result validate_header_context(
        const Chain& headers,
        const consensus_params& params);

    explicit validated_header_context(header_context context) noexcept : m_context{context} {}

    header_context m_context;
};

enum class header_context_evidence_code {
    genesis_parent_not_null,
    non_contiguous_ancestry,
};

class invalid_header_context_evidence
{
public:
    [[nodiscard]] static constexpr invalid_header_context_evidence make(
        header_context_evidence_code code,
        static_text reason) noexcept
    {
        return invalid_header_context_evidence{code, reason};
    }

    [[nodiscard]] constexpr header_context_evidence_code code() const noexcept { return m_code; }
    [[nodiscard]] constexpr std::string_view reason() const noexcept { return m_reason.view(); }

    friend constexpr bool operator==(
        const invalid_header_context_evidence&,
        const invalid_header_context_evidence&) noexcept = default;

private:
    constexpr invalid_header_context_evidence(
        header_context_evidence_code code,
        static_text reason) noexcept : m_code{code},
                                       m_reason{reason}
    {
    }

    header_context_evidence_code m_code;
    static_text m_reason;
};

enum class header_context_result_state {
    valid,
    invalid_evidence,
    operation_error,
};

class header_context_result
{
public:
    [[nodiscard]] static header_context_result valid(validated_header_context context)
    {
        return header_context_result{std::move(context)};
    }

    [[nodiscard]] static header_context_result invalid(invalid_header_context_evidence evidence)
    {
        return header_context_result{evidence};
    }

    [[nodiscard]] static header_context_result failed(operation_error error)
    {
        return header_context_result{error};
    }

    [[nodiscard]] header_context_result_state state() const noexcept
    {
        if (std::holds_alternative<validated_header_context>(m_value)) {
            return header_context_result_state::valid;
        }
        if (std::holds_alternative<invalid_header_context_evidence>(m_value)) {
            return header_context_result_state::invalid_evidence;
        }
        return header_context_result_state::operation_error;
    }

    [[nodiscard]] bool has_value() const noexcept { return state() == header_context_result_state::valid; }
    [[nodiscard]] bool has_invalid_evidence() const noexcept { return state() == header_context_result_state::invalid_evidence; }
    [[nodiscard]] bool has_error() const noexcept { return state() == header_context_result_state::operation_error; }

    [[nodiscard]] const validated_header_context& assume_value() const& { return std::get<validated_header_context>(m_value); }
    [[nodiscard]] validated_header_context&& assume_value() && { return std::get<validated_header_context>(std::move(m_value)); }
    [[nodiscard]] const invalid_header_context_evidence& assume_invalid_evidence() const& { return std::get<invalid_header_context_evidence>(m_value); }
    [[nodiscard]] const operation_error& assume_error() const& { return std::get<operation_error>(m_value); }

private:
    explicit header_context_result(validated_header_context context) : m_value{std::move(context)} {}
    explicit header_context_result(invalid_header_context_evidence evidence) : m_value{evidence} {}
    explicit header_context_result(operation_error error) : m_value{error} {}

    std::variant<validated_header_context, invalid_header_context_evidence, operation_error> m_value;
};

enum class evidence_verify_result_state {
    checked,
    invalid_evidence,
    operation_error,
};

template <typename T>
class evidence_verify_result
{
public:
    [[nodiscard]] static evidence_verify_result checked(validation_decision<T> decision)
    {
        return evidence_verify_result{std::move(decision)};
    }

    [[nodiscard]] static evidence_verify_result invalid(invalid_header_context_evidence evidence)
    {
        return evidence_verify_result{evidence};
    }

    [[nodiscard]] static evidence_verify_result failed(operation_error error)
    {
        return evidence_verify_result{error};
    }

    [[nodiscard]] evidence_verify_result_state state() const noexcept
    {
        if (std::holds_alternative<validation_decision<T>>(m_value)) {
            return evidence_verify_result_state::checked;
        }
        if (std::holds_alternative<invalid_header_context_evidence>(m_value)) {
            return evidence_verify_result_state::invalid_evidence;
        }
        return evidence_verify_result_state::operation_error;
    }

    [[nodiscard]] bool has_value() const noexcept { return state() == evidence_verify_result_state::checked; }
    [[nodiscard]] bool has_invalid_evidence() const noexcept { return state() == evidence_verify_result_state::invalid_evidence; }
    [[nodiscard]] bool has_error() const noexcept { return state() == evidence_verify_result_state::operation_error; }

    [[nodiscard]] const validation_decision<T>& assume_value() const& { return std::get<validation_decision<T>>(m_value); }
    [[nodiscard]] validation_decision<T>&& assume_value() && { return std::get<validation_decision<T>>(std::move(m_value)); }
    [[nodiscard]] const invalid_header_context_evidence& assume_invalid_evidence() const& { return std::get<invalid_header_context_evidence>(m_value); }
    [[nodiscard]] const operation_error& assume_error() const& { return std::get<operation_error>(m_value); }

private:
    explicit evidence_verify_result(validation_decision<T> decision) : m_value{std::move(decision)} {}
    explicit evidence_verify_result(invalid_header_context_evidence evidence) : m_value{evidence} {}
    explicit evidence_verify_result(operation_error error) : m_value{error} {}

    std::variant<validation_decision<T>, invalid_header_context_evidence, operation_error> m_value;
};

template <chain_view Chain>
[[nodiscard]] header_context_result validate_header_context(
    const Chain& headers,
    const consensus_params& params);

// Expert building block for callers that already hold a checked parent
// context. Normal consumers should use validate_header_context(...) or the
// chain-view verify(...) overloads so ancestry evidence is classified.
[[nodiscard]] verify_result<header_facts> assess_header_with_context(
    const block_header& header,
    const header_context& context,
    validation_time time,
    const consensus_params& params);

namespace validation_support {

// Support functions kept visible only because public templates need their
// contracts. The release-facing operation names are the free verify(...)
// overloads in this namespace's parent.

[[nodiscard]] target_bits compact_work_limit(proof_of_work_limit limit) noexcept;

template <chain_view Chain>
[[nodiscard]] constexpr median_time_past previous_median_time_past(const Chain& ancestors)
{
    std::array<block_time, 11> window{};
    std::size_t count{0};
    auto begin{std::ranges::begin(ancestors)};
    auto end{std::ranges::end(ancestors)};
    while (end != begin && count < window.size()) {
        --end;
        const block_header header{*end};
        window[count++] = header.time();
    }
    auto values{std::span{window}.first(count)};
    std::ranges::sort(values);
    return count == 0 ?
               median_time_past{} :
               median_time_past{values[count / 2].as_sys_seconds()};
}

} // namespace validation_support

template <chain_view Chain>
[[nodiscard]] header_context_result validate_header_context(
    const Chain& headers,
    const consensus_params& params)
{
    try {
        const auto ancestor_count{std::ranges::size(headers)};
        if (ancestor_count > static_cast<std::ranges::range_size_t<const Chain>>(
                                 std::numeric_limits<std::int32_t>::max())) {
            return header_context_result::failed(operation_error::make(
                operation_error_code::unsupported_operation,
                "ancestor chain height exceeds validation library range"));
        }

        header_context result;
        result.height = block_height{static_cast<std::int32_t>(ancestor_count)};
        result.previous_median_time_past = validation_support::previous_median_time_past(headers);
        if (ancestor_count == 0) {
            return header_context_result::valid(validated_header_context{result});
        }

        const auto begin{std::ranges::begin(headers)};
        const auto header_at{[&](std::ranges::range_size_t<const Chain> index) {
            return block_header{*(begin + index)};
        }};
        if (header_at(0).previous_block_hash() != block_hash{}) {
            return header_context_result::invalid(invalid_header_context_evidence::make(
                header_context_evidence_code::genesis_parent_not_null,
                "ancestor chain genesis header has a non-null parent"));
        }

        for (std::ranges::range_size_t<const Chain> index{1}; index < ancestor_count; ++index) {
            if (header_at(index).previous_block_hash() != header_at(index - 1).hash()) {
                return header_context_result::invalid(invalid_header_context_evidence::make(
                    header_context_evidence_code::non_contiguous_ancestry,
                    "ancestor chain is not contiguous"));
            }
        }

        const auto parent{header_at(ancestor_count - 1)};
        result.has_parent = true;
        result.parent_hash = parent.hash();
        result.parent_time = parent.time();
        result.parent_target = target_bits{parent.bits()};

        const auto interval{params.difficulty_adjustment_interval()};
        if (interval <= 0) {
            return header_context_result::valid(validated_header_context{result});
        }

        const auto interval_size{static_cast<std::ranges::range_size_t<const Chain>>(interval)};
        if (ancestor_count >= interval_size) {
            const auto first_period_block{header_at(ancestor_count - interval_size)};
            result.has_first_difficulty_period_block = true;
            result.first_difficulty_period_block_time = first_period_block.time();
            result.first_difficulty_period_block_target = target_bits{first_period_block.bits()};
        }

        const auto pow_limit_bits{validation_support::compact_work_limit(params.pow_limit).value()};
        for (auto index{ancestor_count}; index > 0; --index) {
            const auto height{index - 1};
            const auto candidate{header_at(height)};
            if (candidate.bits() != pow_limit_bits || static_cast<std::int64_t>(height) % interval == 0) {
                result.has_last_non_min_difficulty_block = true;
                result.last_non_min_difficulty_block_target = target_bits{candidate.bits()};
                break;
            }
        }

        return header_context_result::valid(validated_header_context{result});
    } catch (const std::bad_alloc&) {
        return header_context_result::failed(operation_error::make(
            operation_error_code::resource_exhaustion,
            "header context validation exhausted resources"));
    } catch (...) {
        return header_context_result::failed(operation_error::make(
            operation_error_code::callback_failure,
            "header chain view threw an exception"));
    }
}

[[nodiscard]] inline verify_result<header_facts> verify(
    const block_header& header,
    const validated_header_context& ancestor_context,
    validation_time time,
    const consensus_params& params)
{
    return verify_exception_boundary<header_facts>([&]() -> verify_result<header_facts> {
        return assess_header_with_context(header, ancestor_context.context(), time, params);
    },
                                                   "header validation exhausted resources", operation_error_code::internal_bug, "header validation threw an exception");
}

[[nodiscard]] inline verify_result<header_facts> verify(
    const block_header& header,
    validation_time time,
    const consensus_params& params)
{
    std::array<block_header, 0> no_ancestors{};
    auto ancestor_context{validate_header_context(
        std::span<const block_header>{no_ancestors},
        params)};
    if (!ancestor_context.has_value()) {
        if (ancestor_context.has_error()) {
            return verify_result<header_facts>::failed(ancestor_context.assume_error());
        }
        return verify_result<header_facts>::failed(operation_error::make(
            operation_error_code::invariant_violation,
            "empty header ancestry produced invalid context evidence"));
    }
    return verify(header, ancestor_context.assume_value(), time, params);
}

template <chain_view Chain>
[[nodiscard]] evidence_verify_result<header_facts> verify(
    const block_header& header,
    const Chain& genesis_to_parent,
    validation_time time,
    const consensus_params& params)
{
    auto ancestor_context{validate_header_context(genesis_to_parent, params)};
    if (!ancestor_context.has_value()) {
        if (ancestor_context.has_invalid_evidence()) {
            return evidence_verify_result<header_facts>::invalid(
                ancestor_context.assume_invalid_evidence());
        }
        return evidence_verify_result<header_facts>::failed(ancestor_context.assume_error());
    }

    auto result{verify(header, ancestor_context.assume_value(), time, params)};
    if (result.has_error()) {
        return evidence_verify_result<header_facts>::failed(result.assume_error());
    }
    return evidence_verify_result<header_facts>::checked(std::move(result).assume_value());
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_HEADER_H
