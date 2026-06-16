// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/coins_view_spend_state.h>

#include <chain.h>
#include <chainstate.h>
#include <coins.h>
#include <undo.h>
#include <validation/block_coin_effects.h>
#include <validation/block_index_adapters.h>

#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace validation {
namespace {

std::optional<int64_t> PreviousMedianTimePastForCoin(const CBlockIndex& block_index, int coin_height)
{
    const int ancestor_height{std::max(coin_height - 1, 0)};
    const CBlockIndex* ancestor{nullptr};

    // Production block connection has a full block index with skip pointers. Use
    // that path for old spends; walking pprev from the tip for every input is
    // quadratic in the common case where old coins are spent in later blocks.
    if (block_index.pskip) {
        ancestor = block_index.GetAncestor(ancestor_height);
    } else {
        // Some tests build partial CBlockIndex stubs without skip pointers.
        // Keep a small fallback for those local fixtures.
        ancestor = &block_index;
        while (ancestor && ancestor->nHeight > ancestor_height) {
            ancestor = ancestor->pprev;
        }
    }

    if (!ancestor || ancestor->nHeight != ancestor_height) return std::nullopt;
    return ancestor->GetMedianTimePast();
}

std::optional<int64_t> PreviousMedianTimePastForCoin(
    const CBlockIndex& block_index,
    int coin_height,
    std::unordered_map<int, int64_t>& cache)
{
    const int ancestor_height{std::max(coin_height - 1, 0)};
    if (const auto cached{cache.find(ancestor_height)}; cached != cache.end()) {
        return cached->second;
    }

    const auto previous_median_time_past{PreviousMedianTimePastForCoin(block_index, coin_height)};
    if (!previous_median_time_past) return std::nullopt;

    cache.emplace(ancestor_height, *previous_median_time_past);
    return previous_median_time_past;
}

} // namespace

bool CoinsViewSpendState::HaveCoin(const COutPoint& outpoint) const
{
    return m_coins.HaveCoin(outpoint);
}

CoinsViewSpendState::CoinsViewSpendState(const CCoinsViewCache& coins)
    : m_coins{coins}
{
}

std::optional<Consensus::CoinSnapshot> CoinsViewSpendState::GetCoin(const COutPoint& outpoint) const
{
    const auto coin{m_coins.GetCoin(outpoint)};
    if (!coin) return std::nullopt;
    return Consensus::CoinSnapshot{
        .output = coin->out,
        .height = static_cast<int>(coin->nHeight),
        .is_coinbase = coin->IsCoinBase(),
    };
}

CoinsViewSequenceLockTimeView::CoinsViewSequenceLockTimeView(int64_t previous_median_time_past)
    : m_previous_median_time_past{previous_median_time_past}
{
}

CoinsViewSequenceLockTimeView::CoinsViewSequenceLockTimeView(
    int64_t previous_median_time_past,
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint)
    : m_previous_median_time_past{previous_median_time_past},
      m_previous_median_time_past_by_outpoint{std::move(previous_median_time_past_by_outpoint)}
{
}

CoinsViewSequenceLockTimeView::CoinsViewSequenceLockTimeView(const CBlockIndex& block_index)
    : m_previous_median_time_past{block_index.pprev ? block_index.pprev->GetMedianTimePast() : 0},
      m_block_index{&block_index}
{
}

int64_t CoinsViewSequenceLockTimeView::PreviousMedianTimePast(const COutPoint& outpoint, int coin_height) const
{
    if (!m_previous_median_time_past_by_outpoint.empty()) {
        const auto configured{m_previous_median_time_past_by_outpoint.find(outpoint)};
        if (configured != m_previous_median_time_past_by_outpoint.end()) return configured->second;
    }
    if (m_block_index) {
        return PreviousMedianTimePastForCoin(*m_block_index, coin_height, m_previous_median_time_past_by_coin_height)
            .value_or(m_previous_median_time_past);
    }
    return m_previous_median_time_past;
}

CoinsViewBlockSpendWorkspace::CoinsViewBlockSpendWorkspace(CCoinsViewCache& parent_coins, int64_t previous_median_time_past)
    : m_staged_coins{std::make_unique<CCoinsViewCache>(&parent_coins)},
      m_spend_view{*m_staged_coins},
      m_sequence_lock_times{std::make_shared<CoinsViewSequenceLockTimeView>(previous_median_time_past)}
{
}

CoinsViewBlockSpendWorkspace::CoinsViewBlockSpendWorkspace(
    CCoinsViewCache& parent_coins,
    int64_t previous_median_time_past,
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint)
    : m_staged_coins{std::make_unique<CCoinsViewCache>(&parent_coins)},
      m_spend_view{*m_staged_coins},
      m_sequence_lock_times{std::make_shared<CoinsViewSequenceLockTimeView>(
          previous_median_time_past,
          std::move(previous_median_time_past_by_outpoint))}
{
}

CoinsViewBlockSpendWorkspace::CoinsViewBlockSpendWorkspace(
    CCoinsViewCache& parent_coins,
    std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times)
    : m_staged_coins{std::make_unique<CCoinsViewCache>(&parent_coins)},
      m_spend_view{*m_staged_coins},
      m_sequence_lock_times{std::move(sequence_lock_times)}
{
    assert(m_sequence_lock_times);
}

CoinsViewBlockSpendWorkspace::CoinsViewBlockSpendWorkspace(CCoinsViewCache& parent_coins, const CBlockIndex& block_index)
    : m_staged_coins{std::make_unique<CCoinsViewCache>(&parent_coins)},
      m_spend_view{*m_staged_coins},
      m_sequence_lock_times{std::make_shared<CoinsViewSequenceLockTimeView>(block_index)}
{
}

CoinsViewBlockSpendWorkspace::~CoinsViewBlockSpendWorkspace() = default;

const Consensus::SpendLookupBackend& CoinsViewBlockSpendWorkspace::StagedSpendView() const
{
    return m_spend_view;
}

const Consensus::SequenceLockTimeView& CoinsViewBlockSpendWorkspace::SequenceLockTimes() const
{
    return *m_sequence_lock_times;
}

Consensus::BlockSpendResult<void> CoinsViewBlockSpendWorkspace::StageTransactionEffectsForIntraBlockView(const Consensus::TransactionCoinEffects& coin_effects, unsigned int transaction_index)
{
    CTxUndo undo;
    ApplyTransactionCoinEffectsForBlock(coin_effects, *m_staged_coins, undo);
    if (transaction_index == 0) {
        assert(undo.vprevout.empty());
    }
    return {};
}

CCoinsViewCache& CoinsViewBlockSpendWorkspace::StagedCoins()
{
    return *m_staged_coins;
}

CoinsViewBlockSpendBackend::CoinsViewBlockSpendBackend(
    CCoinsViewCache& parent_coins,
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint)
    : m_parent_coins{parent_coins},
      m_previous_median_time_past_by_outpoint{std::move(previous_median_time_past_by_outpoint)}
{
}

Consensus::BlockSpendResult<std::unique_ptr<Consensus::SpendWorkspace>> CoinsViewBlockSpendBackend::BeginBlockSpend(const Consensus::BlockSpendContext& context)
{
    std::unique_ptr<Consensus::SpendWorkspace> workspace{std::make_unique<CoinsViewBlockSpendWorkspace>(m_parent_coins, context.previous_median_time_past, m_previous_median_time_past_by_outpoint)};
    return std::move(workspace);
}

CoreSegmentUtxoSnapshotBackend::CoreSegmentUtxoSnapshotBackend(
    std::vector<Consensus::SegmentSpentOutputLookupResult> spent_outputs,
    std::vector<Consensus::SegmentCreatedOutputLookupResult> created_outputs)
{
    for (Consensus::SegmentSpentOutputLookupResult& spent_output : spent_outputs) {
        m_spent_outputs.emplace(KeyFor(spent_output.lookup), std::move(spent_output.coin));
    }
    for (Consensus::SegmentCreatedOutputLookupResult& created_output : created_outputs) {
        m_created_outputs.emplace(KeyFor(created_output.created_output), std::move(created_output.existing_coin));
    }
}

CoreSegmentUtxoSnapshotBackend::SpentLookupKey CoreSegmentUtxoSnapshotBackend::KeyFor(const Consensus::SegmentSpentOutputLookup& lookup)
{
    return {
        .outpoint = lookup.lookup.outpoint,
        .block_index = lookup.block_index,
        .transaction_index = lookup.lookup.transaction_index,
        .input_index = lookup.lookup.input_index,
    };
}

CoreSegmentUtxoSnapshotBackend::CreatedLookupKey CoreSegmentUtxoSnapshotBackend::KeyFor(const Consensus::SegmentCreatedOutput& output)
{
    return {
        .outpoint = output.outpoint,
        .block_index = output.block_index,
        .transaction_index = output.transaction_index,
        .output_index = output.output_index,
    };
}

std::vector<Consensus::SegmentSpentOutputLookupResult> CoreSegmentUtxoSnapshotBackend::LookupSpentOutputs(
    std::span<const Consensus::SegmentBlockView>,
    std::span<const Consensus::SegmentSpentOutputLookup> lookups) const
{
    std::vector<Consensus::SegmentSpentOutputLookupResult> results;
    results.reserve(lookups.size());

    for (const Consensus::SegmentSpentOutputLookup& lookup : lookups) {
        std::optional<Consensus::SegmentCoinSnapshot> coin;
        if (const auto found{m_spent_outputs.find(KeyFor(lookup))}; found != m_spent_outputs.end()) {
            coin = found->second;
        }
        results.push_back({
            .lookup = lookup,
            .coin = std::move(coin),
        });
    }
    return results;
}

std::vector<Consensus::SegmentCreatedOutputLookupResult> CoreSegmentUtxoSnapshotBackend::LookupCreatedOutputs(
    std::span<const Consensus::SegmentBlockView>,
    std::span<const Consensus::SegmentCreatedOutput> created_outputs) const
{
    std::vector<Consensus::SegmentCreatedOutputLookupResult> results;
    results.reserve(created_outputs.size());

    for (const Consensus::SegmentCreatedOutput& created_output : created_outputs) {
        std::optional<Consensus::CoinSnapshot> existing_coin;
        if (const auto found{m_created_outputs.find(KeyFor(created_output))}; found != m_created_outputs.end()) {
            existing_coin = found->second;
        }
        results.push_back({
            .created_output = created_output,
            .existing_coin = std::move(existing_coin),
        });
    }
    return results;
}

std::vector<Consensus::SegmentSpentOutputLookupResult> SnapshotCoreSegmentSpentOutputs(
    ChainstateManager& chainman,
    std::span<const Consensus::SegmentBlockView> blocks,
    std::span<const Consensus::SegmentSpentOutputLookup> lookups)
{
    AssertLockHeld(::cs_main);

    CoreBlockIndexStore block_index{chainman};
    const CoinsViewSpendState spend_state{chainman.ActiveChainstate().CoinsTip()};

    std::vector<std::unique_ptr<const CoinsViewSequenceLockTimeView>> sequence_lock_times;
    sequence_lock_times.reserve(blocks.size());
    for (const Consensus::SegmentBlockView& block : blocks) {
        if (const CBlockIndex* block_index_entry{block_index.LookupBlockIndex(block.context.hash)}) {
            sequence_lock_times.push_back(std::make_unique<CoinsViewSequenceLockTimeView>(*block_index_entry));
        } else {
            sequence_lock_times.push_back(nullptr);
        }
    }

    std::vector<Consensus::SegmentSpentOutputLookupResult> results;
    results.reserve(lookups.size());

    for (const Consensus::SegmentSpentOutputLookup& lookup : lookups) {
        std::optional<Consensus::SegmentCoinSnapshot> result;
        if (lookup.block_index < blocks.size()) {
            if (const std::optional<Consensus::CoinSnapshot> coin{spend_state.GetCoin(lookup.lookup.outpoint)}) {
                if (const auto& sequence_lock_time{sequence_lock_times[lookup.block_index]}) {
                    result = Consensus::SegmentCoinSnapshot{
                        .coin = *coin,
                        .previous_median_time_past = sequence_lock_time->PreviousMedianTimePast(lookup.lookup.outpoint, coin->height),
                    };
                }
            }
        }
        results.push_back({
            .lookup = lookup,
            .coin = std::move(result),
        });
    }
    return results;
}

std::vector<Consensus::SegmentCreatedOutputLookupResult> SnapshotCoreSegmentCreatedOutputs(
    ChainstateManager& chainman,
    std::span<const Consensus::SegmentCreatedOutput> created_outputs)
{
    AssertLockHeld(::cs_main);

    const CoinsViewSpendState spend_state{chainman.ActiveChainstate().CoinsTip()};
    std::vector<Consensus::SegmentCreatedOutputLookupResult> results;
    results.reserve(created_outputs.size());

    for (const Consensus::SegmentCreatedOutput& created_output : created_outputs) {
        results.push_back({
            .created_output = created_output,
            .existing_coin = spend_state.GetCoin(created_output.outpoint),
        });
    }
    return results;
}

} // namespace validation
