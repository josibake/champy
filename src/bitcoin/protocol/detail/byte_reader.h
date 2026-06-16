// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_DETAIL_BYTE_READER_H
#define BITCOIN_BITCOIN_PROTOCOL_DETAIL_BYTE_READER_H

#include <bitcoin/protocol/result.h>

#include <cstddef>
#include <span>

namespace bitcoin::protocol_detail {

class byte_reader
{
public:
    explicit constexpr byte_reader(std::span<const std::byte> bytes) noexcept : m_bytes{bytes} {}

    [[nodiscard]] constexpr std::size_t offset() const noexcept { return m_offset; }
    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return m_bytes.size() - m_offset; }
    [[nodiscard]] constexpr bool ok() const noexcept { return m_ok; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_offset == m_bytes.size(); }
    [[nodiscard]] constexpr malformed_parse failure() const noexcept { return m_failure; }

    [[nodiscard]] constexpr std::byte peek() const noexcept
    {
        return m_offset < m_bytes.size() ? m_bytes[m_offset] : std::byte{0};
    }

    [[nodiscard]] constexpr std::span<const std::byte> read(std::size_t size) noexcept
    {
        if (!m_ok) {
            return {};
        }
        if (size > m_bytes.size() - m_offset) {
            fail(parse_failure_code::truncated);
            return {};
        }
        auto result{m_bytes.subspan(m_offset, size)};
        m_offset += size;
        return result;
    }

    constexpr void fail(parse_failure_code code) noexcept
    {
        if (m_ok) {
            m_ok = false;
            m_failure = malformed_parse{code, m_offset};
        }
    }

private:
    std::span<const std::byte> m_bytes;
    std::size_t m_offset{0};
    bool m_ok{true};
    malformed_parse m_failure{parse_failure_code::truncated, 0};
};

} // namespace bitcoin::protocol_detail

#endif // BITCOIN_BITCOIN_PROTOCOL_DETAIL_BYTE_READER_H
