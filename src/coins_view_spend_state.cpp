// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins_view_spend_state.h>

#include <chain.h>
#include <coins.h>
#include <block_coin_effects.h>
#include <undo.h>

#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace Consensus {
namespace {

std::optional<int64_t> PreviousMedianTimePastForCoin(const CBlockIndex& block_index, int coin_height)
{
    const int ancestor_height{std::max(coin_height - 1, 0)};
    const CBlockIndex* ancestor{nullptr};

    // Production ConnectBlock has a full block index with skip pointers. Use
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

std::optional<CoinSnapshot> CoinsViewSpendState::GetCoin(const COutPoint& outpoint) const
{
    const auto coin{m_coins.GetCoin(outpoint)};
    if (!coin) return std::nullopt;
    return CoinSnapshot{
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
      m_sequence_lock_times{previous_median_time_past}
{
}

CoinsViewBlockSpendWorkspace::CoinsViewBlockSpendWorkspace(
    CCoinsViewCache& parent_coins,
    int64_t previous_median_time_past,
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint)
    : m_staged_coins{std::make_unique<CCoinsViewCache>(&parent_coins)},
      m_spend_view{*m_staged_coins},
      m_sequence_lock_times{
          previous_median_time_past,
          std::move(previous_median_time_past_by_outpoint)}
{
}

CoinsViewBlockSpendWorkspace::CoinsViewBlockSpendWorkspace(CCoinsViewCache& parent_coins, const CBlockIndex& block_index)
    : m_staged_coins{std::make_unique<CCoinsViewCache>(&parent_coins)},
      m_spend_view{*m_staged_coins},
      m_sequence_lock_times{block_index}
{
}

CoinsViewBlockSpendWorkspace::~CoinsViewBlockSpendWorkspace() = default;

const SpendStateView& CoinsViewBlockSpendWorkspace::StagedSpendView() const
{
    return m_spend_view;
}

const SequenceLockTimeView& CoinsViewBlockSpendWorkspace::SequenceLockTimes() const
{
    return m_sequence_lock_times;
}

BlockSpendResult<void> CoinsViewBlockSpendWorkspace::StageTransactionEffectsForIntraBlockView(const TransactionCoinEffects& coin_effects, unsigned int transaction_index)
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

BlockSpendResult<std::unique_ptr<BlockSpendWorkspace>> CoinsViewBlockSpendBackend::BeginBlockSpend(const BlockSpendContext& context)
{
    std::unique_ptr<BlockSpendWorkspace> workspace{std::make_unique<CoinsViewBlockSpendWorkspace>(m_parent_coins, context.previous_median_time_past, m_previous_median_time_past_by_outpoint)};
    return std::move(workspace);
}

} // namespace Consensus
