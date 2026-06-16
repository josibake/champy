// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SNAPSHOT_SPEND_STATE_H
#define BITCOIN_CONSENSUS_SNAPSHOT_SPEND_STATE_H

#include <consensus/block_commit.h>

#include <map>
#include <memory>

namespace Consensus {

class SnapshotSpendWorkspace final : public BlockSpendWorkspace, public SpendStateView {
public:
    SnapshotSpendWorkspace(
        std::map<COutPoint, CoinSnapshot> coins,
        int64_t previous_median_time_past,
        std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);

    [[nodiscard]] const SpendStateView& StagedSpendView() const override { return *this; }
    [[nodiscard]] const SequenceLockTimeView& SequenceLockTimes() const override { return m_sequence_lock_times; }
    [[nodiscard]] bool HaveCoin(const COutPoint& outpoint) const override;
    [[nodiscard]] std::optional<CoinSnapshot> GetCoin(const COutPoint& outpoint) const override;
    [[nodiscard]] BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(const TransactionCoinEffects& effects, unsigned int transaction_index) override;

private:
    class SnapshotSequenceLockTimeView final : public SequenceLockTimeView {
    public:
        SnapshotSequenceLockTimeView(
            int64_t previous_median_time_past,
            std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);

        [[nodiscard]] int64_t PreviousMedianTimePast(const COutPoint& outpoint, int coin_height) const override;
        void Erase(const COutPoint& outpoint);

    private:
        int64_t m_previous_median_time_past{0};
        std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
    };

    std::map<COutPoint, CoinSnapshot> m_coins;
    SnapshotSequenceLockTimeView m_sequence_lock_times;
};

class SnapshotSpendState final : public BlockSpendBackend, public SpendStateView, public BlockSpendStateCommitter {
public:
    void AddCoin(const COutPoint& outpoint, CoinSnapshot coin);
    void AddCoin(const COutPoint& outpoint, CoinSnapshot coin, int64_t previous_median_time_past);

    [[nodiscard]] bool HaveCoin(const COutPoint& outpoint) const override;
    [[nodiscard]] std::optional<CoinSnapshot> GetCoin(const COutPoint& outpoint) const override;
    [[nodiscard]] SnapshotSpendWorkspace MakeWorkspace(int64_t previous_median_time_past = 0) const;
    [[nodiscard]] BlockSpendResult<std::unique_ptr<BlockSpendWorkspace>> BeginBlockSpend(const BlockSpendContext& context) override;
    [[nodiscard]] BlockCommitResult<void> CommitSpendState(const BlockCommitContext& context, const BlockSpendEffects& effects) override;

private:
    std::map<COutPoint, CoinSnapshot> m_coins;
    std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SNAPSHOT_SPEND_STATE_H
