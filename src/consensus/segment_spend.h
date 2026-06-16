// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SEGMENT_SPEND_H
#define BITCOIN_CONSENSUS_SEGMENT_SPEND_H

#include <arith_uint256.h>
#include <consensus/amount.h>
#include <consensus/block_spend.h>
#include <consensus/spend_state_batch.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Consensus {

struct SegmentBlockContext {
    uint256 hash;
    uint256 parent_hash;
    int height{-1};
    int64_t previous_median_time_past{0};
    BlockSpendConsensusOptions spend_options{};
    CAmount block_subsidy{0};
};

struct SegmentBlockView {
    SegmentBlockContext context;
    std::span<const CTransactionRef> transactions;
};

struct SegmentCreatedOutput {
    COutPoint outpoint;
    CoinSnapshot coin;
    std::size_t block_index{0};
    std::size_t transaction_index{0};
    std::size_t output_index{0};
    int64_t previous_median_time_past{0};
};

struct SegmentSpentOutputLookup {
    BlockSpentOutputLookup lookup;
    std::size_t block_index{0};
};

struct SegmentCoinSnapshot {
    CoinSnapshot coin;
    int64_t previous_median_time_past{0};
};

struct SegmentSpentOutputLookupResult {
    SegmentSpentOutputLookup lookup;
    std::optional<SegmentCoinSnapshot> coin;
};

struct SegmentCreatedOutputLookupResult {
    SegmentCreatedOutput created_output;
    std::optional<CoinSnapshot> existing_coin;
};

class SegmentSpendBatchView
{
public:
    virtual ~SegmentSpendBatchView() = default;

    // Results must correspond one-for-one with the requested outpoints, in
    // order. A shorter or longer result is a runtime/backend mismatch, not a
    // consensus missing-input fact.
    [[nodiscard]] virtual std::vector<std::optional<SegmentCoinSnapshot>> GetCoins(std::span<const COutPoint> outpoints) const = 0;
};

class SegmentSpendBatchViewAdapter final : public SegmentSpendBatchView
{
public:
    SegmentSpendBatchViewAdapter(const SpendLookupBatchBackend& spend_state, const SequenceLockTimeView& sequence_lock_times)
        : m_spend_state{spend_state}, m_sequence_lock_times{sequence_lock_times}
    {
    }

    [[nodiscard]] std::vector<std::optional<SegmentCoinSnapshot>> GetCoins(std::span<const COutPoint> outpoints) const override;

private:
    const SpendLookupBatchBackend& m_spend_state;
    const SequenceLockTimeView& m_sequence_lock_times;
};

class SegmentUtxoSnapshotBackend
{
public:
    virtual ~SegmentUtxoSnapshotBackend() = default;

    // Lookup results must correspond one-for-one with the requested items, in
    // order. The snapshot is segment-attempt-local: validation may read it
    // freely, but dropping failed work must not mutate parent chain state.
    [[nodiscard]] virtual std::vector<SegmentSpentOutputLookupResult> LookupSpentOutputs(
        std::span<const SegmentBlockView> blocks,
        std::span<const SegmentSpentOutputLookup> lookups) const = 0;
    [[nodiscard]] virtual std::vector<SegmentCreatedOutputLookupResult> LookupCreatedOutputs(
        std::span<const SegmentBlockView> blocks,
        std::span<const SegmentCreatedOutput> created_outputs) const = 0;
};

class SegmentBatchViewUtxoSnapshotBackend final : public SegmentUtxoSnapshotBackend
{
public:
    explicit SegmentBatchViewUtxoSnapshotBackend(const SegmentSpendBatchView& spend_state) : m_spend_state{spend_state} {}

    [[nodiscard]] std::vector<SegmentSpentOutputLookupResult> LookupSpentOutputs(
        std::span<const SegmentBlockView> blocks,
        std::span<const SegmentSpentOutputLookup> lookups) const override;
    [[nodiscard]] std::vector<SegmentCreatedOutputLookupResult> LookupCreatedOutputs(
        std::span<const SegmentBlockView> blocks,
        std::span<const SegmentCreatedOutput> created_outputs) const override;

private:
    const SegmentSpendBatchView& m_spend_state;
};

enum class SegmentSpentOutputSource {
    External,
    EarlierSegmentOutput,
    SameOrLaterSegmentOutput,
};

struct SegmentSpentOutputDependency {
    SegmentSpentOutputLookup lookup;
    SegmentCreatedOutput created_output;
    SegmentSpentOutputSource source{SegmentSpentOutputSource::External};
};

struct SegmentSpendInputPlan {
    std::vector<SegmentCreatedOutput> created_outputs;
    std::vector<SegmentCreatedOutput> external_overwrite_checks;
    std::vector<SegmentSpentOutputLookup> external_lookups;
    std::vector<SegmentSpentOutputDependency> intra_segment_dependencies;
    std::vector<SegmentSpentOutputDependency> invalid_order_dependencies;
    std::vector<SegmentSpentOutputLookup> duplicate_spends;
};

enum class SegmentSpentOutputJoinStatus {
    Complete,
    MissingOrSpent,
    DuplicateSpend,
    InvalidSpendOrder,
    BackendMismatch,
};

struct SegmentSpentOutputJoin {
    SegmentSpentOutputJoinStatus status{SegmentSpentOutputJoinStatus::Complete};
    std::optional<SegmentSpentOutputLookup> failed_lookup;
    // Indexed by block, then transaction, then input. Coinbase entries are empty.
    std::vector<std::vector<std::vector<SegmentCoinSnapshot>>> input_coins_by_block;
};

struct SegmentSpendSummary {
    std::vector<SegmentCreatedOutput> created_outputs;
    std::vector<SegmentSpentOutputLookupResult> spent_outputs;
};

struct SegmentSpendValidation {
    SegmentSpentOutputJoin joined_inputs;
    std::vector<BlockSpendStageResult> block_stages;
    SegmentSpendSummary summary;
};

struct SegmentChainstateArtifact {
    uint256 best_block;
    int height{-1};
    uint256 accumulator_root;
    std::size_t created_outputs{0};
    std::size_t spent_outputs{0};
};

class SegmentAccumulator
{
public:
    virtual ~SegmentAccumulator() = default;

    virtual void AddCreatedOutput(const SegmentCreatedOutput& output) = 0;
    virtual void AddSpentOutput(const SegmentSpentOutputLookupResult& input) = 0;
    [[nodiscard]] virtual uint256 Root() const = 0;
};

using SegmentSpendAccumulator = SegmentAccumulator;

class AdditiveSegmentAccumulator final : public SegmentAccumulator
{
public:
    void AddCreatedOutput(const SegmentCreatedOutput& output) override;
    void AddSpentOutput(const SegmentSpentOutputLookupResult& input) override;
    [[nodiscard]] uint256 Root() const override;

private:
    arith_uint256 m_sum;
};

[[nodiscard]] std::vector<SegmentCreatedOutput> ExtractSegmentCreatedOutputs(SegmentBlockView block, std::size_t block_index);
[[nodiscard]] std::vector<SegmentSpentOutputLookup> ExtractSegmentSpentOutputLookups(SegmentBlockView block, std::size_t block_index);
[[nodiscard]] SegmentSpendInputPlan PlanSegmentSpendInputs(std::span<const SegmentBlockView> blocks);
[[nodiscard]] std::vector<COutPoint> OutpointsForSegmentSpentOutputLookups(std::span<const SegmentSpentOutputLookup> lookups);
[[nodiscard]] std::vector<COutPoint> OutpointsForSegmentCreatedOutputs(std::span<const SegmentCreatedOutput> created_outputs);
[[nodiscard]] std::vector<SegmentSpentOutputLookupResult> LookupSegmentSpentOutputs(std::span<const SegmentSpentOutputLookup> lookups, const SegmentSpendBatchView& spend_state);
[[nodiscard]] std::vector<SegmentCreatedOutputLookupResult> LookupSegmentCreatedOutputs(std::span<const SegmentCreatedOutput> created_outputs, const SegmentSpendBatchView& spend_state);
[[nodiscard]] std::vector<SegmentSpentOutputLookupResult> LookupSegmentSpentOutputs(std::span<const SegmentBlockView> blocks, std::span<const SegmentSpentOutputLookup> lookups, const SegmentUtxoSnapshotBackend& utxo_snapshot);
[[nodiscard]] std::vector<SegmentCreatedOutputLookupResult> LookupSegmentCreatedOutputs(std::span<const SegmentBlockView> blocks, std::span<const SegmentCreatedOutput> created_outputs, const SegmentUtxoSnapshotBackend& utxo_snapshot);
[[nodiscard]] BlockSpendResult<void> CheckSegmentNoUnspentOutputOverwrite(
    std::span<const SegmentBlockView> blocks,
    const SegmentSpendInputPlan& input_plan,
    const SegmentUtxoSnapshotBackend& utxo_snapshot);
[[nodiscard]] SegmentSpentOutputJoin JoinSegmentSpentOutputs(std::span<const SegmentBlockView> blocks, const SegmentSpendBatchView& spend_state);
[[nodiscard]] SegmentSpentOutputJoin JoinSegmentSpentOutputs(std::span<const SegmentBlockView> blocks, const SegmentUtxoSnapshotBackend& utxo_snapshot);
[[nodiscard]] BlockSpendResult<BlockSpendStageResult> ValidateResolvedSegmentBlockSpend(
    SegmentBlockView block,
    const SegmentSpentOutputJoin& joined,
    std::size_t block_index,
    ScriptCheckPlanCollection script_check_plans);
[[nodiscard]] BlockSpendResult<SegmentSpendValidation> ValidateSegmentSpend(
    std::span<const SegmentBlockView> blocks,
    const SegmentSpendBatchView& spend_state,
    ScriptCheckPlanCollection script_check_plans);
[[nodiscard]] BlockSpendResult<SegmentSpendValidation> ValidateSegmentSpend(
    std::span<const SegmentBlockView> blocks,
    const SegmentUtxoSnapshotBackend& utxo_snapshot,
    ScriptCheckPlanCollection script_check_plans);
[[nodiscard]] SegmentSpendSummary SummarizeSegmentSpend(std::span<const SegmentBlockView> blocks, const SegmentSpentOutputJoin& joined);
[[nodiscard]] SegmentChainstateArtifact FinalizeSegmentSpend(
    std::span<const SegmentBlockView> blocks,
    const SegmentSpendSummary& summary,
    SegmentAccumulator& accumulator);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SEGMENT_SPEND_H
