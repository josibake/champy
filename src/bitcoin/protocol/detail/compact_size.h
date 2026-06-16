// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_DETAIL_COMPACT_SIZE_H
#define BITCOIN_BITCOIN_PROTOCOL_DETAIL_COMPACT_SIZE_H

#include <bitcoin/protocol/detail/byte_reader.h>
#include <bitcoin/protocol/detail/endian.h>

#include <cstdint>

namespace bitcoin::protocol_detail {

[[nodiscard]] constexpr std::uint64_t read_compact_size(byte_reader& reader) noexcept
{
    const auto first_span{reader.read(1)};
    if (!reader.ok()) {
        return 0;
    }

    const auto first{std::to_integer<unsigned char>(first_span[0])};
    if (first < 253U) {
        return first;
    }

    if (first == 253U) {
        const auto bytes{reader.read(2)};
        if (!reader.ok()) {
            return 0;
        }
        const auto value{read_little_endian<std::uint16_t>(bytes)};
        if (value < 253U) {
            reader.fail(parse_failure_code::non_canonical_compact_size);
            return 0;
        }
        return value;
    }

    if (first == 254U) {
        const auto bytes{reader.read(4)};
        if (!reader.ok()) {
            return 0;
        }
        const auto value{read_little_endian<std::uint32_t>(bytes)};
        if (value <= 0xffffU) {
            reader.fail(parse_failure_code::non_canonical_compact_size);
            return 0;
        }
        return value;
    }

    const auto bytes{reader.read(8)};
    if (!reader.ok()) {
        return 0;
    }
    const auto value{read_little_endian<std::uint64_t>(bytes)};
    if (value <= 0xffffffffULL) {
        reader.fail(parse_failure_code::non_canonical_compact_size);
        return 0;
    }
    return value;
}

[[nodiscard]] constexpr std::size_t compact_size_size(std::uint64_t value) noexcept
{
    if (value < 253U) return 1;
    if (value <= 0xffffU) return 3;
    if (value <= 0xffffffffULL) return 5;
    return 9;
}

} // namespace bitcoin::protocol_detail

#endif // BITCOIN_BITCOIN_PROTOCOL_DETAIL_COMPACT_SIZE_H
