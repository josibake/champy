// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/segment_spend.h>

#include <consensus/predicates.h>
#include <hash.h>

#include <algorithm>
#include <cassert>
#include <map>
#include <optional>
#include <utility>

namespace Consensus {
namespace {

bool IsUnspentSegmentOutput(const CTxOut& output)
{
    return !IsUnspendable(output);
}

bool BlockLookupBefore(const BlockSpentOutputLookup& a, const BlockSpentOutputLookup& b)
{
    return a.transaction_index < b.transaction_index ||
           (a.transaction_index == b.transaction_index && a.input_index < b.input_index);
}

bool SegmentLookupBefore(const SegmentSpentOutputLookup& a, const SegmentSpentOutputLookup& b)
{
    return a.block_index < b.block_index ||
           (a.block_index == b.block_index && BlockLookupBefore(a.lookup, b.lookup));
}

void UpdateFailure(
    SegmentSpentOutputJoinStatus status,
    const SegmentSpentOutputLookup& lookup,
    std::optional<std::pair<SegmentSpentOutputJoinStatus, SegmentSpentOutputLookup>>& failure)
{
    if (!failure || SegmentLookupBefore(lookup, failure->second)) {
        failure = std::pair{status, lookup};
    }
}

uint256 HashSegmentCoin(const COutPoint& outpoint, const CoinSnapshot& coin)
{
    return (HashWriter{} << outpoint << coin.output << coin.height << coin.is_coinbase).GetHash();
}

BlockSpendError MissingOrSpentSegmentInput(SegmentBlockView block, const SegmentSpentOutputLookup& lookup)
{
    assert(lookup.lookup.transaction_index < block.transactions.size());
    const CTransaction& tx{*block.transactions[lookup.lookup.transaction_index]};
    return BlockSpendError{
        .issue = BlockConsensusIssue::Consensus,
        .reject_reason = "bad-txns-inputs-missingorspent",
        .debug_message = "CheckTxInputs: inputs missing/spent in transaction " + tx.GetHash().ToString(),
    };
}

BlockSpendError MissingOrSpentSegmentInput()
{
    return BlockSpendError{
        .issue = BlockConsensusIssue::Consensus,
        .reject_reason = "bad-txns-inputs-missingorspent",
        .debug_message = "CheckTxInputs: inputs missing/spent",
    };
}

std::optional<SegmentSpentOutputLookup> FirstDuplicateSpend(std::span<const SegmentBlockView> blocks)
{
    std::map<COutPoint, SegmentSpentOutputLookup> seen_spends;
    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        for (const SegmentSpentOutputLookup& lookup : ExtractSegmentSpentOutputLookups(blocks[block_index], block_index)) {
            const auto [_, inserted]{seen_spends.emplace(lookup.lookup.outpoint, lookup)};
            if (!inserted) return lookup;
        }
    }
    return std::nullopt;
}

} // namespace

void SwiftSyncAccumulator::AddCreatedOutput(const SegmentCreatedOutput& output)
{
    m_sum += UintToArith256(HashSegmentCoin(output.outpoint, output.coin));
}

void SwiftSyncAccumulator::AddSpentOutput(const SegmentSpentOutputLookupResult& input)
{
    if (!input.coin) return;
    m_sum -= UintToArith256(HashSegmentCoin(input.lookup.lookup.outpoint, input.coin->coin));
}

uint256 SwiftSyncAccumulator::Root() const
{
    return ArithToUint256(m_sum);
}

std::vector<std::optional<SegmentCoinSnapshot>> SegmentSpendBatchViewAdapter::GetCoins(std::span<const COutPoint> outpoints) const
{
    std::vector<std::optional<CoinSnapshot>> coins{m_spend_state.GetCoins(outpoints)};
    assert(coins.size() == outpoints.size());

    std::vector<std::optional<SegmentCoinSnapshot>> result;
    result.reserve(coins.size());
    for (std::size_t i{0}; i < coins.size(); ++i) {
        std::optional<CoinSnapshot>& coin{coins[i]};
        if (!coin) {
            result.push_back(std::nullopt);
            continue;
        }
        const int coin_height{coin->height};
        result.push_back(SegmentCoinSnapshot{
            .coin = std::move(*coin),
            .previous_median_time_past = m_sequence_lock_times.PreviousMedianTimePast(outpoints[i], coin_height),
        });
    }
    return result;
}

std::vector<SegmentCreatedOutput> ExtractSegmentCreatedOutputs(SegmentBlockView block, std::size_t block_index)
{
    std::vector<SegmentCreatedOutput> outputs;
    for (std::size_t transaction_index{0}; transaction_index < block.transactions.size(); ++transaction_index) {
        const CTransaction& tx{*block.transactions[transaction_index]};
        const Txid txid{tx.GetHash()};
        for (std::size_t output_index{0}; output_index < tx.vout.size(); ++output_index) {
            const CTxOut& output{tx.vout[output_index]};
            if (!IsUnspentSegmentOutput(output)) continue;
            outputs.push_back({
                .outpoint = COutPoint{txid, static_cast<uint32_t>(output_index)},
                .coin = CoinSnapshot{
                    .output = output,
                    .height = block.context.height,
                    .is_coinbase = IsCoinbase(tx),
                },
                .block_index = block_index,
                .transaction_index = transaction_index,
                .output_index = output_index,
                .previous_median_time_past = block.context.previous_median_time_past,
            });
        }
    }
    return outputs;
}

std::vector<SegmentSpentOutputLookup> ExtractSegmentSpentOutputLookups(SegmentBlockView block, std::size_t block_index)
{
    std::vector<SegmentSpentOutputLookup> lookups;
    for (BlockSpentOutputLookup lookup : ExtractBlockSpentOutputLookups(block.transactions)) {
        lookups.push_back({
            .lookup = std::move(lookup),
            .block_index = block_index,
        });
    }
    return lookups;
}

SegmentSpendInputPlan PlanSegmentSpendInputs(std::span<const SegmentBlockView> blocks)
{
    std::map<COutPoint, SegmentCreatedOutput> created_by_outpoint;
    SegmentSpendInputPlan plan;

    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        std::vector<SegmentCreatedOutput> block_outputs{ExtractSegmentCreatedOutputs(blocks[block_index], block_index)};
        for (SegmentCreatedOutput& output : block_outputs) {
            created_by_outpoint.emplace(output.outpoint, output);
            plan.created_outputs.push_back(std::move(output));
        }
    }

    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        for (SegmentSpentOutputLookup lookup : ExtractSegmentSpentOutputLookups(blocks[block_index], block_index)) {
            const auto created{created_by_outpoint.find(lookup.lookup.outpoint)};
            if (created == created_by_outpoint.end()) {
                plan.external_lookups.push_back(std::move(lookup));
                continue;
            }

            const SegmentCreatedOutput& created_output{created->second};
            SegmentSpentOutputDependency dependency{
                .lookup = lookup,
                .created_output = created_output,
            };
            const bool spends_earlier_segment_output{
                created_output.block_index < lookup.block_index ||
                (created_output.block_index == lookup.block_index &&
                 created_output.transaction_index < lookup.lookup.transaction_index)};
            if (spends_earlier_segment_output) {
                dependency.source = SegmentSpentOutputSource::EarlierSegmentOutput;
                plan.intra_segment_dependencies.push_back(std::move(dependency));
            } else {
                dependency.source = SegmentSpentOutputSource::SameOrLaterSegmentOutput;
                plan.invalid_order_dependencies.push_back(std::move(dependency));
            }
        }
    }

    return plan;
}

std::vector<COutPoint> OutpointsForSegmentSpentOutputLookups(std::span<const SegmentSpentOutputLookup> lookups)
{
    std::vector<COutPoint> outpoints;
    outpoints.reserve(lookups.size());
    for (const SegmentSpentOutputLookup& lookup : lookups) {
        outpoints.push_back(lookup.lookup.outpoint);
    }
    return outpoints;
}

std::vector<SegmentSpentOutputLookupResult> LookupSegmentSpentOutputs(std::span<const SegmentSpentOutputLookup> lookups, const SegmentSpendBatchView& spend_state)
{
    const std::vector<COutPoint> outpoints{OutpointsForSegmentSpentOutputLookups(lookups)};
    std::vector<std::optional<SegmentCoinSnapshot>> coins{spend_state.GetCoins(outpoints)};
    assert(coins.size() == lookups.size());

    std::vector<SegmentSpentOutputLookupResult> results;
    results.reserve(lookups.size());
    for (std::size_t i{0}; i < lookups.size(); ++i) {
        results.push_back({
            .lookup = lookups[i],
            .coin = i < coins.size() ? std::move(coins[i]) : std::nullopt,
        });
    }
    return results;
}

SegmentSpentOutputJoin JoinSegmentSpentOutputs(std::span<const SegmentBlockView> blocks, const SegmentSpendBatchView& spend_state)
{
    SegmentSpentOutputJoin joined;
    joined.input_coins_by_block.resize(blocks.size());
    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        joined.input_coins_by_block[block_index].resize(blocks[block_index].transactions.size());
        for (std::size_t transaction_index{0}; transaction_index < blocks[block_index].transactions.size(); ++transaction_index) {
            const CTransaction& tx{*blocks[block_index].transactions[transaction_index]};
            if (!IsCoinbase(tx)) {
                joined.input_coins_by_block[block_index][transaction_index].resize(tx.vin.size());
            }
        }
    }

    const SegmentSpendInputPlan plan{PlanSegmentSpendInputs(blocks)};
    std::optional<std::pair<SegmentSpentOutputJoinStatus, SegmentSpentOutputLookup>> failure;

    if (const std::optional<SegmentSpentOutputLookup> duplicate{FirstDuplicateSpend(blocks)}) {
        UpdateFailure(SegmentSpentOutputJoinStatus::DuplicateSpend, *duplicate, failure);
    }

    if (!plan.invalid_order_dependencies.empty()) {
        UpdateFailure(SegmentSpentOutputJoinStatus::InvalidSpendOrder, plan.invalid_order_dependencies.front().lookup, failure);
    }

    for (SegmentSpentOutputLookupResult& result : LookupSegmentSpentOutputs(plan.external_lookups, spend_state)) {
        if (!result.coin) {
            UpdateFailure(SegmentSpentOutputJoinStatus::MissingOrSpent, result.lookup, failure);
            continue;
        }
        joined.input_coins_by_block[result.lookup.block_index][result.lookup.lookup.transaction_index][result.lookup.lookup.input_index] = std::move(*result.coin);
    }

    for (const SegmentSpentOutputDependency& dependency : plan.intra_segment_dependencies) {
        const SegmentSpentOutputLookup& lookup{dependency.lookup};
        joined.input_coins_by_block[lookup.block_index][lookup.lookup.transaction_index][lookup.lookup.input_index] = SegmentCoinSnapshot{
            .coin = dependency.created_output.coin,
            .previous_median_time_past = dependency.created_output.previous_median_time_past,
        };
    }

    if (failure) {
        joined.status = failure->first;
        joined.failed_lookup = failure->second;
    }
    return joined;
}

namespace {

class SegmentSequenceLockTimeView final : public SequenceLockTimeView {
public:
    SegmentSequenceLockTimeView(int64_t default_previous_median_time_past, std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint)
        : m_default_previous_median_time_past{default_previous_median_time_past},
          m_previous_median_time_past_by_outpoint{std::move(previous_median_time_past_by_outpoint)}
    {
    }

    int64_t PreviousMedianTimePast(const COutPoint& outpoint, int) const override
    {
        const auto configured{m_previous_median_time_past_by_outpoint.find(outpoint)};
        if (configured != m_previous_median_time_past_by_outpoint.end()) return configured->second;
        return m_default_previous_median_time_past;
    }

private:
    int64_t m_default_previous_median_time_past{0};
    std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
};

} // namespace

BlockSpendResult<BlockSpendStageResult> ValidateResolvedSegmentBlockSpend(
    SegmentBlockView block,
    const SegmentSpentOutputJoin& joined,
    std::size_t block_index,
    ScriptCheckPlanCollection script_check_plans)
{
    assert(block_index < joined.input_coins_by_block.size());
    if (joined.status != SegmentSpentOutputJoinStatus::Complete) {
        assert(joined.failed_lookup);
        if (joined.failed_lookup && joined.failed_lookup->block_index == block_index) {
            return Unexpected<BlockSpendError>{MissingOrSpentSegmentInput(block, *joined.failed_lookup)};
        }
        return Unexpected<BlockSpendError>{MissingOrSpentSegmentInput()};
    }

    BlockSpendStageResult stage;
    BlockSpendEffects& effects{stage.effects};
    effects.transaction_effects.reserve(block.transactions.size());
    if (script_check_plans == ScriptCheckPlanCollection::Collect) {
        stage.script_checks.reserve(block.transactions.size() > 0 ? block.transactions.size() - 1 : 0);
    }

    for (std::size_t transaction_index{0}; transaction_index < block.transactions.size(); ++transaction_index) {
        const CTransactionRef& tx{block.transactions[transaction_index]};
        effects.inputs += tx->vin.size();

        std::vector<CoinSnapshot> input_coins;
        std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint;
        const std::vector<SegmentCoinSnapshot>& resolved_inputs{joined.input_coins_by_block[block_index][transaction_index]};
        input_coins.reserve(resolved_inputs.size());
        for (std::size_t input_index{0}; input_index < resolved_inputs.size(); ++input_index) {
            input_coins.push_back(resolved_inputs[input_index].coin);
            previous_median_time_past_by_outpoint.emplace(
                tx->vin[input_index].prevout,
                resolved_inputs[input_index].previous_median_time_past);
        }

        const SegmentSequenceLockTimeView sequence_lock_times{
            block.context.previous_median_time_past,
            std::move(previous_median_time_past_by_outpoint)};
        const TransactionSpendContext transaction_context{
            .block_height = block.context.height,
            .previous_median_time_past = block.context.previous_median_time_past,
            .sequence_lock_times = sequence_lock_times,
        };
        auto spend_result{EvaluateTransactionSpendForBlock(
            tx,
            input_coins,
            transaction_context,
            block.context.spend_options,
            BlockSpendAccounting{.fees = effects.fees, .sigop_cost = effects.sigop_cost})};
        if (!spend_result) {
            return Unexpected<BlockSpendError>{std::move(spend_result).error()};
        }

        if (script_check_plans == ScriptCheckPlanCollection::Collect && !IsCoinbase(*tx)) {
            stage.script_checks.push_back(BuildTransactionScriptCheckPlan(tx, input_coins, block.context.spend_options.script_flags));
        }

        effects.fees = spend_result->accounting.fees;
        effects.sigop_cost = spend_result->accounting.sigop_cost;
        effects.transaction_effects.push_back(std::move(spend_result->coin_effects));
    }

    const CAmount block_reward{effects.fees + block.context.block_subsidy};
    const auto coinbase_check{CheckCoinbasePaysNoMoreThan(*block.transactions.front(), block_reward)};
    if (!coinbase_check) {
        return Unexpected<BlockSpendError>{coinbase_check.error()};
    }
    return stage;
}

BlockSpendResult<SegmentSpendValidation> ValidateSegmentSpend(
    std::span<const SegmentBlockView> blocks,
    const SegmentSpendBatchView& spend_state,
    ScriptCheckPlanCollection script_check_plans)
{
    SegmentSpentOutputJoin joined{JoinSegmentSpentOutputs(blocks, spend_state)};
    if (joined.status != SegmentSpentOutputJoinStatus::Complete) {
        assert(joined.failed_lookup);
        if (!joined.failed_lookup || joined.failed_lookup->block_index >= blocks.size()) {
            return Unexpected<BlockSpendError>{MissingOrSpentSegmentInput()};
        }
        return Unexpected<BlockSpendError>{MissingOrSpentSegmentInput(blocks[joined.failed_lookup->block_index], *joined.failed_lookup)};
    }

    std::vector<BlockSpendStageResult> block_stages;
    block_stages.reserve(blocks.size());
    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        auto stage{ValidateResolvedSegmentBlockSpend(blocks[block_index], joined, block_index, script_check_plans)};
        if (!stage) {
            return Unexpected<BlockSpendError>{std::move(stage).error()};
        }
        block_stages.push_back(std::move(*stage));
    }

    SegmentSpendSummary summary{SummarizeSegmentSpend(blocks, joined)};
    return SegmentSpendValidation{
        .joined_inputs = std::move(joined),
        .block_stages = std::move(block_stages),
        .summary = std::move(summary),
    };
}

SegmentSpendSummary SummarizeSegmentSpend(std::span<const SegmentBlockView> blocks, const SegmentSpentOutputJoin& joined)
{
    SegmentSpendInputPlan plan{PlanSegmentSpendInputs(blocks)};
    SegmentSpendSummary summary{
        .created_outputs = std::move(plan.created_outputs),
        .spent_outputs = {},
    };

    if (joined.status != SegmentSpentOutputJoinStatus::Complete) return summary;

    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        for (std::size_t transaction_index{0}; transaction_index < blocks[block_index].transactions.size(); ++transaction_index) {
            const CTransaction& tx{*blocks[block_index].transactions[transaction_index]};
            if (IsCoinbase(tx)) continue;

            const std::vector<SegmentCoinSnapshot>& input_coins{joined.input_coins_by_block[block_index][transaction_index]};
            assert(input_coins.size() == tx.vin.size());
            for (std::size_t input_index{0}; input_index < tx.vin.size(); ++input_index) {
                summary.spent_outputs.push_back({
                    .lookup = SegmentSpentOutputLookup{
                        .lookup = BlockSpentOutputLookup{
                            .outpoint = tx.vin[input_index].prevout,
                            .transaction_index = transaction_index,
                            .input_index = input_index,
                        },
                        .block_index = block_index,
                    },
                    .coin = input_coins[input_index],
                });
            }
        }
    }
    return summary;
}

SegmentChainstateArtifact FinalizeSegmentSpend(
    std::span<const SegmentBlockView> blocks,
    const SegmentSpendSummary& summary,
    SegmentSpendAccumulator& accumulator)
{
    for (const SegmentCreatedOutput& output : summary.created_outputs) {
        accumulator.AddCreatedOutput(output);
    }
    for (const SegmentSpentOutputLookupResult& input : summary.spent_outputs) {
        accumulator.AddSpentOutput(input);
    }

    if (blocks.empty()) {
        return {
            .best_block = {},
            .height = -1,
            .accumulator_root = accumulator.Root(),
            .created_outputs = summary.created_outputs.size(),
            .spent_outputs = summary.spent_outputs.size(),
        };
    }

    const SegmentBlockView& last_block{blocks.back()};
    return {
        .best_block = last_block.context.hash,
        .height = last_block.context.height,
        .accumulator_root = accumulator.Root(),
        .created_outputs = summary.created_outputs.size(),
        .spent_outputs = summary.spent_outputs.size(),
    };
}

} // namespace Consensus
