// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_CONTEXT_H
#define BITCOIN_BITCOIN_VALIDATION_CONTEXT_H

#include <bitcoin/protocol/amount.h>
#include <bitcoin/protocol/chain_view.h>
#include <bitcoin/protocol/coin_index.h>
#include <bitcoin/protocol/hash.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bitcoin {

class validation_time
{
public:
    constexpr validation_time() noexcept = default;
    constexpr explicit validation_time(std::chrono::sys_seconds value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr std::chrono::sys_seconds value() const noexcept { return m_value; }

    friend constexpr bool operator==(const validation_time&, const validation_time&) noexcept = default;
    friend constexpr auto operator<=>(const validation_time&, const validation_time&) noexcept = default;

private:
    std::chrono::sys_seconds m_value{};
};

class target_bits
{
public:
    constexpr target_bits() noexcept = default;
    constexpr explicit target_bits(std::uint32_t value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return m_value; }

    friend constexpr bool operator==(const target_bits&, const target_bits&) noexcept = default;

private:
    std::uint32_t m_value{0};
};

class proof_of_work_limit
{
public:
    constexpr proof_of_work_limit() noexcept = default;
    constexpr explicit proof_of_work_limit(std::array<std::byte, 32> value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr const std::array<std::byte, 32>& bytes() const noexcept { return m_value; }

    friend constexpr bool operator==(const proof_of_work_limit&, const proof_of_work_limit&) noexcept = default;

private:
    std::array<std::byte, 32> m_value{};
};

class verification_flags
{
public:
    using mask_type = std::uint64_t;

    constexpr verification_flags() noexcept = default;

    [[nodiscard]] static constexpr verification_flags none() noexcept { return verification_flags{}; }
    [[nodiscard]] static constexpr verification_flags p2sh() noexcept { return verification_flags{p2sh_mask}; }
    [[nodiscard]] static constexpr verification_flags dersig() noexcept { return verification_flags{dersig_mask}; }
    [[nodiscard]] static constexpr verification_flags nulldummy() noexcept { return verification_flags{nulldummy_mask}; }
    [[nodiscard]] static constexpr verification_flags checklocktimeverify() noexcept { return verification_flags{checklocktimeverify_mask}; }
    [[nodiscard]] static constexpr verification_flags checksequenceverify() noexcept { return verification_flags{checksequenceverify_mask}; }
    [[nodiscard]] static constexpr verification_flags witness() noexcept { return verification_flags{witness_mask}; }
    [[nodiscard]] static constexpr verification_flags taproot() noexcept { return verification_flags{taproot_mask}; }
    [[nodiscard]] static constexpr verification_flags all() noexcept { return verification_flags{known_mask}; }

    [[nodiscard]] static constexpr std::optional<verification_flags> from_mask(mask_type mask) noexcept
    {
        if (!known(mask)) {
            return std::nullopt;
        }
        return verification_flags{mask};
    }

    [[nodiscard]] static constexpr bool known(mask_type mask) noexcept
    {
        return (mask & ~known_mask) == 0;
    }

    [[nodiscard]] constexpr mask_type mask() const noexcept { return m_mask; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_mask == 0; }
    [[nodiscard]] constexpr bool contains(verification_flags flags) const noexcept
    {
        return (m_mask & flags.m_mask) == flags.m_mask;
    }

    friend constexpr bool operator==(const verification_flags&, const verification_flags&) noexcept = default;
    friend constexpr verification_flags operator|(verification_flags left, verification_flags right) noexcept
    {
        return verification_flags{left.m_mask | right.m_mask};
    }
    friend constexpr verification_flags operator&(verification_flags left, verification_flags right) noexcept
    {
        return verification_flags{left.m_mask & right.m_mask};
    }

private:
    constexpr explicit verification_flags(mask_type mask) noexcept : m_mask{mask} {}

    static constexpr mask_type p2sh_mask{mask_type{1} << 0};
    static constexpr mask_type dersig_mask{mask_type{1} << 2};
    static constexpr mask_type nulldummy_mask{mask_type{1} << 4};
    static constexpr mask_type checklocktimeverify_mask{mask_type{1} << 9};
    static constexpr mask_type checksequenceverify_mask{mask_type{1} << 10};
    static constexpr mask_type witness_mask{mask_type{1} << 11};
    static constexpr mask_type taproot_mask{mask_type{1} << 17};
    static constexpr mask_type known_mask{
        p2sh_mask |
        dersig_mask |
        nulldummy_mask |
        checklocktimeverify_mask |
        checksequenceverify_mask |
        witness_mask |
        taproot_mask};

    mask_type m_mask{0};
};

struct consensus_params {
    proof_of_work_limit pow_limit{};
    std::chrono::seconds target_spacing{600};
    std::chrono::seconds target_timespan{14 * 24 * 60 * 60};
    std::chrono::seconds max_timewarp{600};
    bool allow_min_difficulty_blocks{false};
    bool no_retargeting{false};
    bool enforce_bip94{false};
    std::int32_t minimum_block_version{0};

    [[nodiscard]] constexpr std::int64_t difficulty_adjustment_interval() const noexcept
    {
        if (target_spacing.count() <= 0) {
            return 0;
        }
        return target_timespan.count() / target_spacing.count();
    }
};

struct header_context {
    block_height height;
    median_time_past previous_median_time_past;
    bool has_parent{false};
    block_hash parent_hash;
    block_time parent_time;
    target_bits parent_target;
    bool has_first_difficulty_period_block{false};
    block_time first_difficulty_period_block_time;
    target_bits first_difficulty_period_block_target;
    bool has_last_non_min_difficulty_block{false};
    target_bits last_non_min_difficulty_block_target;
};

inline constexpr std::size_t witness_scale_factor{4};

struct transaction_limits {
    amount max_money{amount{2'100'000'000'000'000}};
    std::size_t max_stripped_weight{4'000'000};
};

struct transaction_context {
    transaction_limits limits;
};

struct transaction_finality_context {
    transaction_limits limits;
    block_height height;
    block_time timestamp;
};

class locktime_flags
{
public:
    constexpr locktime_flags() noexcept = default;
    constexpr explicit locktime_flags(std::uint32_t mask) noexcept : m_mask{mask} {}

    [[nodiscard]] constexpr std::uint32_t mask() const noexcept { return m_mask; }
    [[nodiscard]] constexpr bool verify_sequence() const noexcept { return (m_mask & verify_sequence_mask) != 0; }

    [[nodiscard]] static constexpr locktime_flags sequence() noexcept { return locktime_flags{verify_sequence_mask}; }

    friend constexpr bool operator==(const locktime_flags&, const locktime_flags&) noexcept = default;

private:
    static constexpr std::uint32_t verify_sequence_mask{1U};

    std::uint32_t m_mask{0};
};

struct spend_context {
    block_height height;
    median_time_past previous_median_time_past;
    transaction_limits limits;
    locktime_flags locktime;
    std::int32_t coinbase_maturity{100};
};

struct header_facts {
    block_hash hash;
};

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_CONTEXT_H
