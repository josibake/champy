// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_snapshot.h>

#include <chain.h>
#include <coins.h>
#include <consensus/spend_state_batch.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <validation/coins_view_spend_state.h>

#include <optional>
#include <set>

namespace validation {
namespace {

Consensus::CoinSnapshot ToCoinSnapshot(const Coin& coin)
{
    return Consensus::CoinSnapshot{
        .output = coin.out,
        .height = static_cast<int>(coin.nHeight),
        .is_coinbase = coin.IsCoinBase(),
    };
}

std::set<COutPoint> SnapshotOutpointsForBlock(const CBlock& block)
{
    std::set<COutPoint> outpoints;
    const Consensus::BlockSpentOutputLookupPlan plan{Consensus::PlanBlockSpentOutputLookups(block.vtx)};
    for (const Consensus::BlockSpentOutputLookup& lookup : plan.external_lookups) {
        outpoints.insert(lookup.outpoint);
    }

    for (const CTransactionRef& tx : block.vtx) {
        const Txid txid{tx->GetHash()};
        for (uint32_t output_index{0}; output_index < tx->vout.size(); ++output_index) {
            outpoints.emplace(txid, output_index);
        }
    }
    return outpoints;
}

void AddSnapshotCoin(
    SnapshotBlockConnectionState& snapshot,
    const COutPoint& outpoint,
    const CCoinsViewCache& coins,
    const CoinsViewSequenceLockTimeView& sequence_lock_times)
{
    const std::optional<Coin> coin{coins.GetCoin(outpoint)};
    if (!coin) return;

    snapshot.AddCoin(
        outpoint,
        ToCoinSnapshot(*coin),
        sequence_lock_times.PreviousMedianTimePast(outpoint, static_cast<int>(coin->nHeight)));
}

} // namespace

SnapshotBlockConnectionState SnapshotCoreBlockConnectionState(
    const CBlock& block,
    const CBlockIndex& block_index,
    const CCoinsViewCache& coins)
{
    AssertLockHeld(::cs_main);

    SnapshotBlockConnectionState snapshot;
    snapshot.SetBestBlock(block_index.pprev ? block_index.pprev->GetBlockHash() : uint256{});

    const CoinsViewSequenceLockTimeView sequence_lock_times{block_index};
    for (const COutPoint& outpoint : SnapshotOutpointsForBlock(block)) {
        AddSnapshotCoin(snapshot, outpoint, coins, sequence_lock_times);
    }
    return snapshot;
}

SnapshotBlockConnectionState CoreCoinsBlockConnectionSnapshotter::Snapshot(const CBlock& block, const CBlockIndex& block_index) const
{
    AssertLockHeld(::cs_main);
    return SnapshotCoreBlockConnectionState(block, block_index, m_coins);
}

} // namespace validation
