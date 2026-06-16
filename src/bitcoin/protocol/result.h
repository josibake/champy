// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_RESULT_H
#define BITCOIN_BITCOIN_PROTOCOL_RESULT_H

#include <cstddef>
#include <string_view>
#include <utility>
#include <variant>

namespace bitcoin {

class static_text
{
public:
    template <std::size_t Size>
    consteval static_text(const char (&text)[Size]) noexcept : m_value{text, Size - 1}
    {
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return m_value; }

    friend constexpr bool operator==(static_text, static_text) noexcept = default;

private:
    std::string_view m_value;
};

enum class parse_failure_code {
    truncated,
    trailing_data,
    non_canonical_compact_size,
    compact_size_overflow,
    invalid_witness_marker,
};

class malformed_parse
{
public:
    constexpr malformed_parse(parse_failure_code code, std::size_t offset) noexcept :
        m_code{code}, m_offset{offset}
    {
    }

    [[nodiscard]] constexpr parse_failure_code code() const noexcept { return m_code; }
    [[nodiscard]] constexpr std::size_t offset() const noexcept { return m_offset; }

    friend constexpr bool operator==(const malformed_parse&, const malformed_parse&) noexcept = default;

private:
    parse_failure_code m_code;
    std::size_t m_offset;
};

enum class parse_result_state {
    parsed,
    malformed,
};

template <typename T>
class parse_result
{
public:
    [[nodiscard]] static parse_result parsed(T value)
    {
        return parse_result{std::move(value)};
    }

    [[nodiscard]] static parse_result malformed(malformed_parse failure) noexcept
    {
        return parse_result{failure};
    }

    [[nodiscard]] parse_result_state state() const noexcept
    {
        return has_value() ? parse_result_state::parsed : parse_result_state::malformed;
    }
    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(m_value); }
    [[nodiscard]] bool has_failure() const noexcept { return state() == parse_result_state::malformed; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] const T& assume_value() const& { return std::get<T>(m_value); }
    [[nodiscard]] T&& assume_value() && { return std::get<T>(std::move(m_value)); }
    [[nodiscard]] const malformed_parse& assume_failure() const& { return std::get<malformed_parse>(m_value); }

private:
    explicit parse_result(T value) : m_value{std::move(value)} {}
    explicit parse_result(malformed_parse value) noexcept : m_value{value} {}

    std::variant<T, malformed_parse> m_value;
};

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_RESULT_H
