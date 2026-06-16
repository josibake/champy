// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINS_VIEW_SPEND_STATE_H
#define BITCOIN_COINS_VIEW_SPEND_STATE_H

#include <consensus/block_spend.h>
#include <consensus/segment_spend.h>
#include <kernel/cs_main.h>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

class CCoinsViewCache;
class COutPoint;
class CBlockIndex;
class BlockIndexLookup;
class ChainstateManager;

namespace validation {

class CoinsViewSpendState final : public Consensus::SpendLookupBackend
{
public:
    explicit CoinsViewSpendState(const CCoinsViewCache& coins);

    [[nodiscard]] bool HaveCoin(const COutPoint& outpoint) const override;
    [[nodiscard]] std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const override;

private:
    const CCoinsViewCache& m_coins;
};

class CoinsViewSequenceLockTimeView final : public Consensus::SequenceLockTimeView
{
public:
    explicit CoinsViewSequenceLockTimeView(int64_t previous_median_time_past);
    CoinsViewSequenceLockTimeView(
        int64_t previous_median_time_past,
        std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);
    explicit CoinsViewSequenceLockTimeView(const CBlockIndex& block_index);

    [[nodiscard]] int64_t PreviousMedianTimePast(const COutPoint& outpoint, int coin_height) const override;

private:
    int64_t m_previous_median_time_past{0};
    std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
    mutable std::unordered_map<int, int64_t> m_previous_median_time_past_by_coin_height;
    const CBlockIndex* m_block_index{nullptr};
};

class CoinsViewBlockSpendWorkspace final : public Consensus::SpendWorkspace
{
public:
    CoinsViewBlockSpendWorkspace(CCoinsViewCache& parent_coins, int64_t previous_median_time_past);
    CoinsViewBlockSpendWorkspace(
        CCoinsViewCache& parent_coins,
        int64_t previous_median_time_past,
        std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);
    CoinsViewBlockSpendWorkspace(
        CCoinsViewCache& parent_coins,
        std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times);
    CoinsViewBlockSpendWorkspace(CCoinsViewCache& parent_coins, const CBlockIndex& block_index);
    ~CoinsViewBlockSpendWorkspace() override;

    [[nodiscard]] const Consensus::SpendLookupBackend& StagedSpendView() const override;
    [[nodiscard]] const Consensus::SequenceLockTimeView& SequenceLockTimes() const override;
    [[nodiscard]] Consensus::BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(const Consensus::TransactionCoinEffects& coin_effects, unsigned int transaction_index) override;
    [[nodiscard]] CCoinsViewCache& StagedCoins();

private:
    std::unique_ptr<CCoinsViewCache> m_staged_coins;
    CoinsViewSpendState m_spend_view;
    std::shared_ptr<const Consensus::SequenceLockTimeView> m_sequence_lock_times;
};

class CoinsViewBlockSpendBackend final : public Consensus::SpendWorkspaceProvider
{
public:
    explicit CoinsViewBlockSpendBackend(CCoinsViewCache& parent_coins) : m_parent_coins{parent_coins} {}
    CoinsViewBlockSpendBackend(CCoinsViewCache& parent_coins, std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);

    [[nodiscard]] Consensus::BlockSpendResult<std::unique_ptr<Consensus::SpendWorkspace>> BeginBlockSpend(const Consensus::BlockSpendContext& context) override;

private:
    CCoinsViewCache& m_parent_coins;
    std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
};

class CoreSegmentUtxoSnapshotBackend final : public Consensus::SegmentUtxoSnapshotBackend
{
public:
    CoreSegmentUtxoSnapshotBackend(
        std::vector<Consensus::SegmentSpentOutputLookupResult> spent_outputs,
        std::vector<Consensus::SegmentCreatedOutputLookupResult> created_outputs);

    [[nodiscard]] std::vector<Consensus::SegmentSpentOutputLookupResult> LookupSpentOutputs(
        std::span<const Consensus::SegmentBlockView> blocks,
        std::span<const Consensus::SegmentSpentOutputLookup> lookups) const override;
    [[nodiscard]] std::vector<Consensus::SegmentCreatedOutputLookupResult> LookupCreatedOutputs(
        std::span<const Consensus::SegmentBlockView> blocks,
        std::span<const Consensus::SegmentCreatedOutput> created_outputs) const override;

private:
    struct SpentLookupKey {
        COutPoint outpoint;
        std::size_t block_index{0};
        std::size_t transaction_index{0};
        std::size_t input_index{0};

        friend bool operator<(const SpentLookupKey& a, const SpentLookupKey& b)
        {
            if (a.block_index != b.block_index) return a.block_index < b.block_index;
            if (a.transaction_index != b.transaction_index) return a.transaction_index < b.transaction_index;
            if (a.input_index != b.input_index) return a.input_index < b.input_index;
            return a.outpoint < b.outpoint;
        }
    };

    struct CreatedLookupKey {
        COutPoint outpoint;
        std::size_t block_index{0};
        std::size_t transaction_index{0};
        std::size_t output_index{0};

        friend bool operator<(const CreatedLookupKey& a, const CreatedLookupKey& b)
        {
            if (a.block_index != b.block_index) return a.block_index < b.block_index;
            if (a.transaction_index != b.transaction_index) return a.transaction_index < b.transaction_index;
            if (a.output_index != b.output_index) return a.output_index < b.output_index;
            return a.outpoint < b.outpoint;
        }
    };

    [[nodiscard]] static SpentLookupKey KeyFor(const Consensus::SegmentSpentOutputLookup& lookup);
    [[nodiscard]] static CreatedLookupKey KeyFor(const Consensus::SegmentCreatedOutput& output);

    std::map<SpentLookupKey, std::optional<Consensus::SegmentCoinSnapshot>> m_spent_outputs;
    std::map<CreatedLookupKey, std::optional<Consensus::CoinSnapshot>> m_created_outputs;
};

[[nodiscard]] std::vector<Consensus::SegmentSpentOutputLookupResult> SnapshotCoreSegmentSpentOutputs(
    ChainstateManager& chainman,
    std::span<const Consensus::SegmentBlockView> blocks,
    std::span<const Consensus::SegmentSpentOutputLookup> lookups) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
[[nodiscard]] std::vector<Consensus::SegmentCreatedOutputLookupResult> SnapshotCoreSegmentCreatedOutputs(
    ChainstateManager& chainman,
    std::span<const Consensus::SegmentCreatedOutput> created_outputs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

} // namespace validation

#endif // BITCOIN_COINS_VIEW_SPEND_STATE_H
