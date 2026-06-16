// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/api.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace {

class std_bitcoin_chain_view : public std::ranges::view_base
{
public:
    constexpr explicit std_bitcoin_chain_view(std::span<const bitcoin::block_header> headers) noexcept :
        m_headers{headers}
    {
    }

    [[nodiscard]] constexpr auto begin() const noexcept { return m_headers.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return m_headers.end(); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_headers.size(); }
    [[nodiscard]] constexpr const bitcoin::block_header& operator[](std::size_t index) const noexcept
    {
        return m_headers[index];
    }

private:
    std::span<const bitcoin::block_header> m_headers;
};

class std_bitcoin_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint&) const
    {
        return std::nullopt;
    }
};

static_assert(bitcoin::chain_view<std_bitcoin_chain_view>);
static_assert(bitcoin::coin_index<std_bitcoin_coin_index>);

using header_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block_header&>(),
    std::declval<const std_bitcoin_chain_view&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::consensus_params&>()));
static_assert(std::same_as<header_verify_result, bitcoin::evidence_verify_result<bitcoin::header_facts>>);

using transaction_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>()));
static_assert(std::same_as<transaction_verify_result, bitcoin::verify_result<bitcoin::transaction_facts>>);

using transaction_spend_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>(),
    std::declval<const bitcoin::spend_context&>(),
    std::declval<const std_bitcoin_coin_index&>()));
static_assert(std::same_as<transaction_spend_result, bitcoin::verify_result<bitcoin::spend_facts>>);

[[nodiscard]] bitcoin::block_header header(std::int32_t version, std::uint32_t nonce)
{
    return bitcoin::block_header{
        version,
        bitcoin::block_hash{},
        bitcoin::hash256{},
        bitcoin::block_time{1231006505 + nonce},
        0x1d00ffff,
        nonce};
}

} // namespace

int main()
{
    std::array headers{
        header(1, 1),
        header(2, 2),
    };
    const std_bitcoin_chain_view chain{std::span<const bitcoin::block_header>{headers}};
    const std_bitcoin_coin_index coins;

    if (chain.size() != 2 || chain[1].version() != 2) {
        return 1;
    }
    if (coins(bitcoin::outpoint{}).has_value()) {
        return 2;
    }

    return 0;
}
