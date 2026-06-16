// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_DETAIL_ENDIAN_H
#define BITCOIN_BITCOIN_PROTOCOL_DETAIL_ENDIAN_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace bitcoin::protocol_detail {

template <typename UInt>
[[nodiscard]] constexpr UInt read_little_endian(std::span<const std::byte> bytes) noexcept
{
    UInt value{0};
    for (std::size_t i{0}; i < bytes.size(); ++i) {
        value |= static_cast<UInt>(std::to_integer<unsigned char>(bytes[i])) << (8U * i);
    }
    return value;
}

template <typename UInt>
constexpr void write_little_endian(UInt value, std::span<std::byte> out) noexcept
{
    for (std::size_t i{0}; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
    }
}

} // namespace bitcoin::protocol_detail

#endif // BITCOIN_BITCOIN_PROTOCOL_DETAIL_ENDIAN_H
