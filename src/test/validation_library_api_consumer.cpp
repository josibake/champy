// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/api.h>

#include <array>
#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

static_assert(!std::same_as<bitcoin::txid, bitcoin::wtxid>);
static_assert(!std::same_as<bitcoin::txid, bitcoin::block_hash>);
static_assert(!std::same_as<bitcoin::wtxid, bitcoin::block_hash>);

class external_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(bitcoin::outpoint) const
    {
        return std::nullopt;
    }
};

static_assert(bitcoin::coin_index<external_coin_index>);

class external_fallible_coin_source
{
public:
    [[nodiscard]] bitcoin::coin_lookup_result lookup(bitcoin::outpoint) const
    {
        return bitcoin::coin_lookup_result::missing();
    }
};

static_assert(!bitcoin::coin_index<external_fallible_coin_source>);
static_assert(bitcoin::fallible_coin_source<external_fallible_coin_source>);

class optional_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(bitcoin::outpoint) const;
};

static_assert(bitcoin::coin_index<optional_coin_index>);
static_assert(bitcoin::chain_view<std::span<const bitcoin::block_header>>);

using header_genesis_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block_header&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::consensus_params&>()));
static_assert(std::same_as<header_genesis_verify_result, bitcoin::verify_result<bitcoin::header_facts>>);

using header_chain_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block_header&>(),
    std::declval<const std::span<const bitcoin::block_header>&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::consensus_params&>()));
static_assert(std::same_as<header_chain_verify_result, bitcoin::evidence_verify_result<bitcoin::header_facts>>);

using header_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block_header&>(),
    std::declval<const bitcoin::validated_header_context&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::consensus_params&>()));
static_assert(std::same_as<header_verify_result, bitcoin::verify_result<bitcoin::header_facts>>);

using transaction_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>()));
static_assert(std::same_as<transaction_verify_result, bitcoin::verify_result<bitcoin::transaction_facts>>);

using transaction_finality_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>(),
    std::declval<const bitcoin::transaction_finality_context&>()));
static_assert(std::same_as<transaction_finality_result, bitcoin::verify_result<bitcoin::transaction_facts>>);

using transaction_spend_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>(),
    std::declval<const bitcoin::spend_context&>(),
    std::declval<const external_coin_index&>()));
static_assert(std::same_as<transaction_spend_result, bitcoin::verify_result<bitcoin::spend_facts>>);

using transaction_script_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>(),
    std::declval<const bitcoin::spend_context&>(),
    std::declval<bitcoin::script_context>(),
    std::declval<const external_coin_index&>()));
static_assert(std::same_as<transaction_script_result, bitcoin::verify_result<bitcoin::spend_facts>>);

using script_verify_result = decltype(bitcoin::verify_script(
    std::declval<bitcoin::script_ref>(),
    std::declval<bitcoin::amount>(),
    std::declval<const bitcoin::transaction&>(),
    std::declval<std::size_t>(),
    std::declval<bitcoin::verification_flags>(),
    std::declval<std::span<const bitcoin::tx_output>>()));
static_assert(std::same_as<script_verify_result, bitcoin::script_execution_result>);

using block_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block&>()));
static_assert(std::same_as<block_verify_result, bitcoin::verify_result<bitcoin::block_facts>>);

using contextual_block_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block&>(),
    std::declval<const std::span<const bitcoin::block_header>&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::block_validation_context&>()));
static_assert(std::same_as<contextual_block_verify_result, bitcoin::evidence_verify_result<bitcoin::block_facts>>);

using complete_block_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block&>(),
    std::declval<const std::span<const bitcoin::block_header>&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::block_validation_context&>(),
    std::declval<const external_coin_index&>()));
static_assert(std::same_as<complete_block_verify_result, bitcoin::evidence_verify_result<bitcoin::block_facts>>);

using witnessed_complete_block_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block&>(),
    std::declval<const bitcoin::validated_header_context&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::block_validation_context&>(),
    std::declval<const external_coin_index&>()));
static_assert(std::same_as<witnessed_complete_block_verify_result, bitcoin::verify_result<bitcoin::block_facts>>);

} // namespace

int main()
{
    std::vector<bitcoin::block_header> headers;
    std::span<const bitcoin::block_header> chain{headers};
    external_coin_index coins;

    (void)chain;
    (void)coins;
    return 0;
}
