// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_HASH_H
#define BITCOIN_BITCOIN_PROTOCOL_HASH_H

#include <array>
#include <compare>
#include <cstddef>
#include <span>

namespace bitcoin {
namespace detail {

struct hash256_tag;
struct txid_tag;
struct wtxid_tag;
struct block_hash_tag;

template <typename Tag>
class basic_hash_id
{
public:
    constexpr basic_hash_id() noexcept = default;
    constexpr explicit basic_hash_id(std::array<std::byte, 32> bytes) noexcept : m_bytes{bytes} {}

    constexpr explicit basic_hash_id(std::span<const std::byte, 32> bytes) noexcept
    {
        for (std::size_t i{0}; i < bytes.size(); ++i) {
            m_bytes[i] = bytes[i];
        }
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return *this != basic_hash_id{};
    }

    [[nodiscard]] friend constexpr std::span<const std::byte, 32> as_bytes(const basic_hash_id& value) noexcept
    {
        return std::span<const std::byte, 32>{value.m_bytes};
    }

    friend constexpr bool operator==(const basic_hash_id&, const basic_hash_id&) noexcept = default;
    friend constexpr auto operator<=>(const basic_hash_id&, const basic_hash_id&) noexcept = default;

private:
    std::array<std::byte, 32> m_bytes{};
};

} // namespace detail

using hash256 = detail::basic_hash_id<detail::hash256_tag>;
using txid = detail::basic_hash_id<detail::txid_tag>;
using wtxid = detail::basic_hash_id<detail::wtxid_tag>;
using block_hash = detail::basic_hash_id<detail::block_hash_tag>;

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_HASH_H
