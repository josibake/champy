// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/snapshot_spend_state.h>

#include <memory>
#include <utility>

namespace Consensus {
namespace {

BlockSpendError SnapshotSpendStateError(const std::string& reject_reason, const std::string& debug_message)
{
    return BlockSpendError{
        .issue = BlockConsensusIssue::Consensus,
        .reject_reason = reject_reason,
        .debug_message = debug_message,
    };
}

} // namespace

SnapshotSpendWorkspace::SnapshotSequenceLockTimeView::SnapshotSequenceLockTimeView(
    int64_t previous_median_time_past,
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint)
    : m_previous_median_time_past{previous_median_time_past},
      m_previous_median_time_past_by_outpoint{std::move(previous_median_time_past_by_outpoint)}
{
}

int64_t SnapshotSpendWorkspace::SnapshotSequenceLockTimeView::PreviousMedianTimePast(const COutPoint& outpoint, int) const
{
    if (const auto configured{m_previous_median_time_past_by_outpoint.find(outpoint)}; configured != m_previous_median_time_past_by_outpoint.end()) {
        return configured->second;
    }
    return m_previous_median_time_past;
}

void SnapshotSpendWorkspace::SnapshotSequenceLockTimeView::Erase(const COutPoint& outpoint)
{
    m_previous_median_time_past_by_outpoint.erase(outpoint);
}

SnapshotSpendWorkspace::SnapshotSpendWorkspace(
    std::map<COutPoint, CoinSnapshot> coins,
    int64_t previous_median_time_past,
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint)
    : m_coins{std::move(coins)},
      m_sequence_lock_times{previous_median_time_past, std::move(previous_median_time_past_by_outpoint)}
{
}

bool SnapshotSpendWorkspace::HaveCoin(const COutPoint& outpoint) const
{
    return m_coins.contains(outpoint);
}

std::optional<CoinSnapshot> SnapshotSpendWorkspace::GetCoin(const COutPoint& outpoint) const
{
    const auto coin{m_coins.find(outpoint)};
    if (coin == m_coins.end()) return std::nullopt;
    return coin->second;
}

BlockSpendResult<void> SnapshotSpendWorkspace::StageTransactionEffectsForIntraBlockView(const TransactionCoinEffects& effects, unsigned int)
{
    for (const SpentCoinEffect& spend : effects.spends) {
        const auto coin{m_coins.find(spend.outpoint)};
        if (coin == m_coins.end()) {
            return Consensus::Unexpected<BlockSpendError>{SnapshotSpendStateError(
                "bad-txns-inputs-missingorspent",
                "SnapshotSpendWorkspace: staged spend missing coin")};
        }
        m_coins.erase(coin);
        m_sequence_lock_times.Erase(spend.outpoint);
    }

    for (const CreatedCoinEffect& create : effects.creates) {
        const auto [_, inserted]{m_coins.emplace(create.outpoint, create.coin)};
        if (!inserted) {
            return Consensus::Unexpected<BlockSpendError>{SnapshotSpendStateError(
                "bad-txns-BIP30",
                "SnapshotSpendWorkspace: staged create overwrote unspent coin")};
        }
    }

    return {};
}

void SnapshotSpendState::AddCoin(const COutPoint& outpoint, CoinSnapshot coin)
{
    m_coins.insert_or_assign(outpoint, std::move(coin));
    m_previous_median_time_past_by_outpoint.erase(outpoint);
}

void SnapshotSpendState::AddCoin(const COutPoint& outpoint, CoinSnapshot coin, int64_t previous_median_time_past)
{
    m_coins.insert_or_assign(outpoint, std::move(coin));
    m_previous_median_time_past_by_outpoint.insert_or_assign(outpoint, previous_median_time_past);
}

bool SnapshotSpendState::HaveCoin(const COutPoint& outpoint) const
{
    return m_coins.contains(outpoint);
}

std::optional<CoinSnapshot> SnapshotSpendState::GetCoin(const COutPoint& outpoint) const
{
    const auto coin{m_coins.find(outpoint)};
    if (coin == m_coins.end()) return std::nullopt;
    return coin->second;
}

SnapshotSpendWorkspace SnapshotSpendState::MakeWorkspace(int64_t previous_median_time_past) const
{
    return SnapshotSpendWorkspace{m_coins, previous_median_time_past, m_previous_median_time_past_by_outpoint};
}

BlockSpendResult<std::unique_ptr<BlockSpendWorkspace>> SnapshotSpendState::BeginBlockSpend(const BlockSpendContext& context)
{
    std::unique_ptr<BlockSpendWorkspace> workspace{std::make_unique<SnapshotSpendWorkspace>(m_coins, context.previous_median_time_past, m_previous_median_time_past_by_outpoint)};
    return std::move(workspace);
}

} // namespace Consensus
