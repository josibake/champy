// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_CORE_ADAPTER_COIN_H
#define BITCOIN_BITCOIN_CORE_ADAPTER_COIN_H

#include <bitcoin/protocol/chain_view.h>
#include <bitcoin/protocol/coin_index.h>
#include <bitcoin/protocol/transaction.h>

#include <optional>
#include <unordered_map>

class CBlockIndex;
class CCoinsView;
class Coin;

namespace bitcoin::core_adapter {

// The Core Coin record does not carry the previous median-time-past required by
// Champy's spend vocabulary, so adapters must supply the value from their chain view.
[[nodiscard]] bitcoin::coin to_coin(const ::Coin& value, median_time_past previous_median_time_past);

class core_coin_snapshot
{
public:
    // Synthetic snapshots may have a fixed sequence-lock time. Real chain-backed
    // snapshots should use the CBlockIndex overload so each coin height gets its
    // own previous median-time-past witness.
    core_coin_snapshot(const CCoinsView& view, median_time_past previous_median_time_past) noexcept;
    core_coin_snapshot(const CCoinsView& view, const CBlockIndex& block_index);

    [[nodiscard]] std::optional<coin> operator()(const outpoint& point) const;

private:
    friend class core_coin_source;

    [[nodiscard]] coin_lookup_result lookup_status(const outpoint& point) const;

    const CCoinsView* m_view;
    median_time_past m_fixed_previous_median_time_past;
    bool m_uses_per_height_mtp{false};
    std::unordered_map<int, median_time_past> m_previous_median_time_past_by_ancestor_height;
};

class core_coin_source
{
public:
    core_coin_source(const CCoinsView& view, median_time_past previous_median_time_past) noexcept;
    core_coin_source(const CCoinsView& view, const CBlockIndex& block_index);

    [[nodiscard]] coin_lookup_result lookup(const outpoint& point) const;

private:
    core_coin_snapshot m_snapshot;
};

static_assert(coin_index<core_coin_snapshot>);
static_assert(!fallible_coin_source<core_coin_snapshot>);
static_assert(!coin_index<core_coin_source>);
static_assert(fallible_coin_source<core_coin_source>);

} // namespace bitcoin::core_adapter

#endif // BITCOIN_BITCOIN_CORE_ADAPTER_COIN_H
