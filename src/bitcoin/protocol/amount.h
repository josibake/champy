// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_AMOUNT_H
#define BITCOIN_BITCOIN_PROTOCOL_AMOUNT_H

#include <compare>
#include <cstdint>

namespace bitcoin {

class amount
{
public:
    constexpr amount() noexcept = default;
    constexpr explicit amount(std::int64_t satoshis) noexcept : m_satoshis{satoshis} {}

    [[nodiscard]] constexpr std::int64_t satoshis() const noexcept { return m_satoshis; }

    friend constexpr bool operator==(const amount&, const amount&) noexcept = default;
    friend constexpr auto operator<=>(const amount&, const amount&) noexcept = default;

private:
    std::int64_t m_satoshis{0};
};

namespace units {
inline constexpr amount satoshi{1};
inline constexpr amount btc{100'000'000};
} // namespace units

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_AMOUNT_H
