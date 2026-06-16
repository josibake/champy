// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_BLOCK_HEADER_H
#define BITCOIN_BITCOIN_PROTOCOL_BLOCK_HEADER_H

#include <chrono>
#include <compare>
#include <cstdint>

#include <bitcoin/protocol/hash.h>

namespace bitcoin {

class block_time
{
public:
    constexpr block_time() noexcept = default;
    constexpr explicit block_time(std::uint32_t seconds_since_epoch) noexcept : m_value{seconds_since_epoch} {}

    [[nodiscard]] constexpr std::uint32_t seconds_since_epoch() const noexcept { return m_value; }
    [[nodiscard]] constexpr std::chrono::sys_seconds as_sys_seconds() const noexcept
    {
        return std::chrono::sys_seconds{std::chrono::seconds{m_value}};
    }

    friend constexpr bool operator==(const block_time&, const block_time&) noexcept = default;
    friend constexpr auto operator<=>(const block_time&, const block_time&) noexcept = default;

private:
    std::uint32_t m_value{0};
};

class block_header
{
public:
    constexpr block_header() noexcept = default;
    constexpr block_header(
        std::int32_t version,
        bitcoin::block_hash previous_block_hash,
        bitcoin::hash256 merkle_root,
        bitcoin::block_time time,
        std::uint32_t bits,
        std::uint32_t nonce) noexcept :
        m_version{version},
        m_previous_block_hash{previous_block_hash},
        m_merkle_root{merkle_root},
        m_time{time},
        m_bits{bits},
        m_nonce{nonce}
    {
    }

    [[nodiscard]] bitcoin::block_hash hash() const;
    [[nodiscard]] constexpr std::int32_t version() const noexcept { return m_version; }
    [[nodiscard]] constexpr bitcoin::block_hash previous_block_hash() const noexcept { return m_previous_block_hash; }
    [[nodiscard]] constexpr bitcoin::hash256 merkle_root() const noexcept { return m_merkle_root; }
    [[nodiscard]] constexpr bitcoin::block_time time() const noexcept { return m_time; }
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return m_bits; }
    [[nodiscard]] constexpr std::uint32_t nonce() const noexcept { return m_nonce; }

    friend constexpr bool operator==(const block_header&, const block_header&) noexcept = default;

private:
    std::int32_t m_version{0};
    bitcoin::block_hash m_previous_block_hash;
    bitcoin::hash256 m_merkle_root;
    bitcoin::block_time m_time;
    std::uint32_t m_bits{0};
    std::uint32_t m_nonce{0};
};

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_BLOCK_HEADER_H
