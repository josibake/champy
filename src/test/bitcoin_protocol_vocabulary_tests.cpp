// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/protocol/api.h>

#include <concepts>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace {

static_assert(std::regular<bitcoin::hash256>);
static_assert(std::regular<bitcoin::txid>);
static_assert(std::regular<bitcoin::wtxid>);
static_assert(std::regular<bitcoin::block_hash>);
static_assert(std::regular<bitcoin::amount>);
static_assert(std::regular<bitcoin::tx_output_index>);
static_assert(std::regular<bitcoin::outpoint>);
static_assert(std::regular<bitcoin::script>);
static_assert(std::regular<bitcoin::witness_item>);
static_assert(std::regular<bitcoin::tx_input>);
static_assert(std::regular<bitcoin::tx_output>);
static_assert(std::regular<bitcoin::transaction>);
static_assert(std::regular<bitcoin::block_time>);
static_assert(std::regular<bitcoin::block_header>);
static_assert(std::regular<bitcoin::block>);
static_assert(std::regular<bitcoin::chain_work>);

static_assert(!std::convertible_to<bitcoin::txid, bitcoin::wtxid>);
static_assert(!std::convertible_to<bitcoin::wtxid, bitcoin::txid>);
static_assert(!std::convertible_to<bitcoin::txid, bitcoin::block_hash>);
static_assert(!std::convertible_to<bitcoin::block_hash, bitcoin::txid>);
static_assert(!std::assignable_from<bitcoin::txid&, bitcoin::wtxid>);
static_assert(!std::assignable_from<bitcoin::block_hash&, bitcoin::txid>);

static_assert(std::same_as<decltype(std::declval<bitcoin::tx_output_index>().value()), std::uint32_t>);
static_assert(std::same_as<decltype(std::declval<bitcoin::block_time>().seconds_since_epoch()), std::uint32_t>);
static_assert(std::totally_ordered<bitcoin::chain_work>);
static_assert(bitcoin::outpoint::null().is_null());
static_assert(!std::constructible_from<bitcoin::script_ref, bitcoin::script&&>);
static_assert(!std::constructible_from<bitcoin::script_ref, const bitcoin::script&&>);

static_assert(std::same_as<
    decltype(std::declval<const bitcoin::transaction&>().inputs()),
    std::span<const bitcoin::tx_input>>);
static_assert(std::same_as<
    decltype(std::declval<const bitcoin::tx_input&>().witness()),
    std::span<const bitcoin::witness_item>>);
static_assert(std::same_as<
    decltype(std::declval<const bitcoin::transaction&>().outputs()),
    std::span<const bitcoin::tx_output>>);
static_assert(std::same_as<
    decltype(std::declval<const bitcoin::block&>().transactions()),
    std::span<const bitcoin::transaction>>);

template <typename T>
concept has_transaction_id = requires(const T& value) { value.id(); };

template <typename T>
concept has_transaction_witness_id = requires(const T& value) { value.witness_id(); };

template <typename T>
concept has_hash = requires(const T& value) { value.hash(); };

static_assert(has_transaction_id<bitcoin::transaction>);
static_assert(has_transaction_witness_id<bitcoin::transaction>);
static_assert(has_hash<bitcoin::block_header>);
static_assert(has_hash<bitcoin::block>);

} // namespace

int main()
{
    return 0;
}
