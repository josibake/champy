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
#include <set>
#include <string>
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

bool LookupBeforeCreatedOutput(const SegmentSpentOutputLookup& lookup, const SegmentCreatedOutput& created)
{
    return lookup.block_index < created.block_index ||
           (lookup.block_index == created.block_index &&
            lookup.lookup.transaction_index <= created.transaction_index);
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

BlockSpendError InvalidSegmentSpend(const std::string& reject_reason, const std::string& debug_message)
{
    return BlockSpendError{
        .issue = BlockConsensusIssue::Consensus,
        .reject_reason = reject_reason,
        .debug_message = debug_message,
    };
}

BlockSpendError SegmentBackendMismatch()
{
    return BlockSpendError{
        .issue = BlockConsensusIssue::ValidationRuntime,
        .runtime_issue = ValidationRuntimeIssue::SystemError,
        .reject_reason = "segment-backend-mismatch",
        .debug_message = "segment spend backend returned results that do not match the requested lookup batch",
    };
}

bool SameLookup(const SegmentSpentOutputLookup& a, const SegmentSpentOutputLookup& b)
{
    return a.block_index == b.block_index &&
           a.lookup.outpoint == b.lookup.outpoint &&
           a.lookup.transaction_index == b.lookup.transaction_index &&
           a.lookup.input_index == b.lookup.input_index;
}

bool SameCreatedOutput(const SegmentCreatedOutput& a, const SegmentCreatedOutput& b)
{
    return a.outpoint == b.outpoint &&
           a.block_index == b.block_index &&
           a.transaction_index == b.transaction_index &&
           a.output_index == b.output_index;
}

bool LookupHasInvalidOrderDependency(
    const SegmentSpentOutputLookup& lookup,
    std::span<const SegmentSpentOutputDependency> dependencies)
{
    return std::ranges::any_of(dependencies, [&](const SegmentSpentOutputDependency& dependency) {
        return SameLookup(lookup, dependency.lookup);
    });
}

std::optional<SegmentCreatedOutput> FirstSameOrLaterCreatedOutput(
    const SegmentSpentOutputLookup& lookup,
    std::span<const SegmentCreatedOutput> created_outputs)
{
    const auto created{std::ranges::find_if(created_outputs, [&](const SegmentCreatedOutput& output) {
        return output.outpoint == lookup.lookup.outpoint &&
               LookupBeforeCreatedOutput(lookup, output);
    })};
    if (created == created_outputs.end()) return std::nullopt;
    return *created;
}

std::vector<SegmentCreatedOutput> ExtractSegmentCreatedOutputs(std::span<const SegmentBlockView> blocks)
{
    std::vector<SegmentCreatedOutput> outputs;
    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        std::vector<SegmentCreatedOutput> block_outputs{ExtractSegmentCreatedOutputs(blocks[block_index], block_index)};
        outputs.reserve(outputs.size() + block_outputs.size());
        for (SegmentCreatedOutput& output : block_outputs) {
            outputs.push_back(std::move(output));
        }
    }
    return outputs;
}

std::vector<SegmentSpentOutputLookup> ExtractBlockSegmentSpentOutputLookups(
    SegmentBlockView block,
    std::size_t block_index,
    std::size_t transaction_index)
{
    std::vector<SegmentSpentOutputLookup> lookups;
    if (transaction_index >= block.transactions.size()) return lookups;

    const CTransaction& tx{*block.transactions[transaction_index]};
    if (IsCoinbase(tx)) return lookups;

    lookups.reserve(tx.vin.size());
    for (std::size_t input_index{0}; input_index < tx.vin.size(); ++input_index) {
        lookups.push_back({
            .lookup = BlockSpentOutputLookup{
                .outpoint = tx.vin[input_index].prevout,
                .transaction_index = transaction_index,
                .input_index = input_index,
            },
            .block_index = block_index,
        });
    }
    return lookups;
}

std::vector<SegmentCreatedOutput> ExtractBlockSegmentCreatedOutputs(
    SegmentBlockView block,
    std::size_t block_index,
    std::size_t transaction_index)
{
    std::vector<SegmentCreatedOutput> outputs;
    if (transaction_index >= block.transactions.size()) return outputs;

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
    return outputs;
}

BlockSpendResult<void> CheckNoUnspentIntraSegmentOutputOverwrite(
    std::span<const SegmentBlockView> blocks,
    const SegmentSpendInputPlan& input_plan)
{
    std::map<COutPoint, SegmentCreatedOutput> unspent_segment_outputs;

    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        if (blocks[block_index].context.spend_options.check_no_unspent_output_overwrite) {
            for (const SegmentCreatedOutput& created_output : input_plan.created_outputs) {
                if (created_output.block_index != block_index) continue;
                if (unspent_segment_outputs.contains(created_output.outpoint)) {
                    return Unexpected<BlockSpendError>{InvalidSegmentSpend("bad-txns-BIP30", "tried to overwrite transaction")};
                }
            }
        }

        const SegmentBlockView& block{blocks[block_index]};
        for (std::size_t transaction_index{0}; transaction_index < block.transactions.size(); ++transaction_index) {
            for (const SegmentSpentOutputLookup& spent_output : ExtractBlockSegmentSpentOutputLookups(block, block_index, transaction_index)) {
                unspent_segment_outputs.erase(spent_output.lookup.outpoint);
            }
            for (SegmentCreatedOutput output : ExtractBlockSegmentCreatedOutputs(block, block_index, transaction_index)) {
                unspent_segment_outputs[output.outpoint] = std::move(output);
            }
        }
    }

    return {};
}

bool LookupWithinSegment(std::span<const SegmentBlockView> blocks, const SegmentSpentOutputLookup& lookup)
{
    if (lookup.block_index >= blocks.size()) return false;
    const SegmentBlockView& block{blocks[lookup.block_index]};
    if (lookup.lookup.transaction_index >= block.transactions.size()) return false;
    const CTransaction& tx{*block.transactions[lookup.lookup.transaction_index]};
    if (IsCoinbase(tx)) return false;
    return lookup.lookup.input_index < tx.vin.size();
}

} // namespace

void AdditiveSegmentAccumulator::AddCreatedOutput(const SegmentCreatedOutput& output)
{
    m_sum += UintToArith256(HashSegmentCoin(output.outpoint, output.coin));
}

void AdditiveSegmentAccumulator::AddSpentOutput(const SegmentSpentOutputLookupResult& input)
{
    if (!input.coin) return;
    m_sum -= UintToArith256(HashSegmentCoin(input.lookup.lookup.outpoint, input.coin->coin));
}

uint256 AdditiveSegmentAccumulator::Root() const
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

std::vector<SegmentSpentOutputLookupResult> SegmentBatchViewUtxoSnapshotBackend::LookupSpentOutputs(
    std::span<const SegmentBlockView>,
    std::span<const SegmentSpentOutputLookup> lookups) const
{
    return Consensus::LookupSegmentSpentOutputs(lookups, m_spend_state);
}

std::vector<SegmentCreatedOutputLookupResult> SegmentBatchViewUtxoSnapshotBackend::LookupCreatedOutputs(
    std::span<const SegmentBlockView>,
    std::span<const SegmentCreatedOutput> created_outputs) const
{
    return Consensus::LookupSegmentCreatedOutputs(created_outputs, m_spend_state);
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
    const std::vector<SegmentCreatedOutput> all_created_outputs{ExtractSegmentCreatedOutputs(blocks)};
    SegmentSpendInputPlan plan;
    plan.created_outputs = all_created_outputs;

    std::map<COutPoint, SegmentCreatedOutput> unspent_segment_outputs;
    std::set<COutPoint> modified_before_transaction;
    std::set<COutPoint> modified_before_block;
    for (std::size_t block_index{0}; block_index < blocks.size(); ++block_index) {
        const SegmentBlockView& block{blocks[block_index]};
        if (block.context.spend_options.check_no_unspent_output_overwrite) {
            for (const SegmentCreatedOutput& output : plan.created_outputs) {
                if (output.block_index != block_index) continue;
                if (!modified_before_block.contains(output.outpoint)) {
                    plan.external_overwrite_checks.push_back(output);
                }
            }
        }

        std::set<COutPoint> modified_in_block;
        for (std::size_t transaction_index{0}; transaction_index < block.transactions.size(); ++transaction_index) {
            for (SegmentSpentOutputLookup lookup : ExtractBlockSegmentSpentOutputLookups(block, block_index, transaction_index)) {
                const auto current_segment_output{unspent_segment_outputs.find(lookup.lookup.outpoint)};
                if (current_segment_output != unspent_segment_outputs.end()) {
                    plan.intra_segment_dependencies.push_back({
                        .lookup = lookup,
                        .created_output = current_segment_output->second,
                        .source = SegmentSpentOutputSource::EarlierSegmentOutput,
                    });
                    unspent_segment_outputs.erase(current_segment_output);
                    modified_before_transaction.insert(lookup.lookup.outpoint);
                    modified_in_block.insert(lookup.lookup.outpoint);
                    continue;
                }

                if (modified_before_transaction.contains(lookup.lookup.outpoint)) {
                    plan.duplicate_spends.push_back(std::move(lookup));
                    continue;
                }

                if (std::optional<SegmentCreatedOutput> same_or_later{FirstSameOrLaterCreatedOutput(lookup, all_created_outputs)}) {
                    plan.invalid_order_dependencies.push_back({
                        .lookup = lookup,
                        .created_output = std::move(*same_or_later),
                        .source = SegmentSpentOutputSource::SameOrLaterSegmentOutput,
                    });
                }

                modified_before_transaction.insert(lookup.lookup.outpoint);
                modified_in_block.insert(lookup.lookup.outpoint);
                plan.external_lookups.push_back(std::move(lookup));
            }

            for (SegmentCreatedOutput output : ExtractBlockSegmentCreatedOutputs(block, block_index, transaction_index)) {
                modified_before_transaction.insert(output.outpoint);
                modified_in_block.insert(output.outpoint);
                unspent_segment_outputs[output.outpoint] = std::move(output);
            }
        }
        modified_before_block.insert(modified_in_block.begin(), modified_in_block.end());
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

std::vector<COutPoint> OutpointsForSegmentCreatedOutputs(std::span<const SegmentCreatedOutput> created_outputs)
{
    std::vector<COutPoint> outpoints;
    outpoints.reserve(created_outputs.size());
    for (const SegmentCreatedOutput& output : created_outputs) {
        outpoints.push_back(output.outpoint);
    }
    return outpoints;
}

std::vector<SegmentSpentOutputLookupResult> LookupSegmentSpentOutputs(std::span<const SegmentSpentOutputLookup> lookups, const SegmentSpendBatchView& spend_state)
{
    const std::vector<COutPoint> outpoints{OutpointsForSegmentSpentOutputLookups(lookups)};
    std::vector<std::optional<SegmentCoinSnapshot>> coins{spend_state.GetCoins(outpoints)};
    assert(coins.size() == lookups.size());
    if (coins.size() != lookups.size()) return {};

    std::vector<SegmentSpentOutputLookupResult> results;
    results.reserve(lookups.size());
    for (std::size_t i{0}; i < lookups.size(); ++i) {
        results.push_back({
            .lookup = lookups[i],
            .coin = std::move(coins[i]),
        });
    }
    return results;
}

std::vector<SegmentCreatedOutputLookupResult> LookupSegmentCreatedOutputs(
    std::span<const SegmentCreatedOutput> created_outputs,
    const SegmentSpendBatchView& spend_state)
{
    const std::vector<COutPoint> outpoints{OutpointsForSegmentCreatedOutputs(created_outputs)};
    std::vector<std::optional<SegmentCoinSnapshot>> coins{spend_state.GetCoins(outpoints)};
    assert(coins.size() == created_outputs.size());
    if (coins.size() != created_outputs.size()) return {};

    std::vector<SegmentCreatedOutputLookupResult> results;
    results.reserve(created_outputs.size());
    for (std::size_t i{0}; i < created_outputs.size(); ++i) {
        std::optional<CoinSnapshot> existing_coin;
        if (coins[i]) {
            existing_coin = std::move(coins[i]->coin);
        }
        results.push_back({
            .created_output = created_outputs[i],
            .existing_coin = std::move(existing_coin),
        });
    }
    return results;
}

std::vector<SegmentSpentOutputLookupResult> LookupSegmentSpentOutputs(
    std::span<const SegmentBlockView> blocks,
    std::span<const SegmentSpentOutputLookup> lookups,
    const SegmentUtxoSnapshotBackend& utxo_snapshot)
{
    return utxo_snapshot.LookupSpentOutputs(blocks, lookups);
}

std::vector<SegmentCreatedOutputLookupResult> LookupSegmentCreatedOutputs(
    std::span<const SegmentBlockView> blocks,
    std::span<const SegmentCreatedOutput> created_outputs,
    const SegmentUtxoSnapshotBackend& utxo_snapshot)
{
    return utxo_snapshot.LookupCreatedOutputs(blocks, created_outputs);
}

namespace {

SegmentSpentOutputJoin JoinSegmentSpentOutputsFromExternalLookups(
    std::span<const SegmentBlockView> blocks,
    std::vector<SegmentSpentOutputLookupResult> external_results)
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

    if (external_results.size() != plan.external_lookups.size()) {
        joined.status = SegmentSpentOutputJoinStatus::BackendMismatch;
        return joined;
    }

    for (const SegmentSpentOutputLookup& duplicate : plan.duplicate_spends) {
        UpdateFailure(SegmentSpentOutputJoinStatus::DuplicateSpend, duplicate, failure);
    }

    for (std::size_t result_index{0}; result_index < external_results.size(); ++result_index) {
        SegmentSpentOutputLookupResult& result{external_results[result_index]};
        if (!SameLookup(result.lookup, plan.external_lookups[result_index]) ||
            !LookupWithinSegment(blocks, result.lookup)) {
            joined.status = SegmentSpentOutputJoinStatus::BackendMismatch;
            joined.failed_lookup = result.lookup;
            return joined;
        }
        if (!result.coin) {
            UpdateFailure(
                LookupHasInvalidOrderDependency(result.lookup, plan.invalid_order_dependencies) ?
                    SegmentSpentOutputJoinStatus::InvalidSpendOrder :
                    SegmentSpentOutputJoinStatus::MissingOrSpent,
                result.lookup,
                failure);
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

} // namespace

BlockSpendResult<void> CheckSegmentNoUnspentOutputOverwrite(
    std::span<const SegmentBlockView> blocks,
    const SegmentSpendInputPlan& input_plan,
    const SegmentUtxoSnapshotBackend& utxo_snapshot)
{
    const auto intra_segment_overwrite_check{CheckNoUnspentIntraSegmentOutputOverwrite(blocks, input_plan)};
    if (!intra_segment_overwrite_check) {
        return Unexpected<BlockSpendError>{intra_segment_overwrite_check.error()};
    }

    std::vector<SegmentCreatedOutput> created_outputs_to_check;
    created_outputs_to_check.reserve(input_plan.external_overwrite_checks.size());
    for (const SegmentCreatedOutput& created_output : input_plan.external_overwrite_checks) {
        if (created_output.block_index >= blocks.size()) {
            return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
        }
        if (!blocks[created_output.block_index].context.spend_options.check_no_unspent_output_overwrite) {
            return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
        }
        created_outputs_to_check.push_back(created_output);
    }
    if (created_outputs_to_check.empty()) return {};

    auto overwrite_results{LookupSegmentCreatedOutputs(blocks, created_outputs_to_check, utxo_snapshot)};
    if (overwrite_results.size() != created_outputs_to_check.size()) {
        return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
    }

    for (std::size_t result_index{0}; result_index < overwrite_results.size(); ++result_index) {
        const SegmentCreatedOutputLookupResult& result{overwrite_results[result_index]};
        const SegmentCreatedOutput& expected{created_outputs_to_check[result_index]};
        if (!SameCreatedOutput(result.created_output, expected)) {
            return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
        }
        if (result.existing_coin) {
            return Unexpected<BlockSpendError>{InvalidSegmentSpend("bad-txns-BIP30", "tried to overwrite transaction")};
        }
    }

    return {};
}

SegmentSpentOutputJoin JoinSegmentSpentOutputs(std::span<const SegmentBlockView> blocks, const SegmentSpendBatchView& spend_state)
{
    const SegmentSpendInputPlan plan{PlanSegmentSpendInputs(blocks)};
    return JoinSegmentSpentOutputsFromExternalLookups(blocks, LookupSegmentSpentOutputs(plan.external_lookups, spend_state));
}

SegmentSpentOutputJoin JoinSegmentSpentOutputs(std::span<const SegmentBlockView> blocks, const SegmentUtxoSnapshotBackend& utxo_snapshot)
{
    const SegmentSpendInputPlan plan{PlanSegmentSpendInputs(blocks)};
    return JoinSegmentSpentOutputsFromExternalLookups(blocks, LookupSegmentSpentOutputs(blocks, plan.external_lookups, utxo_snapshot));
}

namespace {

class SegmentSequenceLockTimeView final : public SequenceLockTimeView
{
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
    if (block.transactions.empty() || !IsCoinbase(*block.transactions.front())) {
        return Unexpected<BlockSpendError>{InvalidSegmentSpend("bad-cb-missing", "first transaction is not a coinbase")};
    }
    if (block_index >= joined.input_coins_by_block.size()) {
        return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
    }
    if (joined.status != SegmentSpentOutputJoinStatus::Complete) {
        if (joined.status == SegmentSpentOutputJoinStatus::BackendMismatch) {
            return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
        }
        if (joined.failed_lookup &&
            joined.failed_lookup->block_index == block_index &&
            joined.failed_lookup->lookup.transaction_index < block.transactions.size()) {
            return Unexpected<BlockSpendError>{MissingOrSpentSegmentInput(block, *joined.failed_lookup)};
        }
        return Unexpected<BlockSpendError>{MissingOrSpentSegmentInput()};
    }
    if (joined.input_coins_by_block[block_index].size() != block.transactions.size()) {
        return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
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
        if (IsCoinbase(*tx)) {
            if (!resolved_inputs.empty()) {
                return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
            }
        } else if (resolved_inputs.size() != tx->vin.size()) {
            return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
        }
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
    const SegmentBatchViewUtxoSnapshotBackend utxo_snapshot{spend_state};
    return ValidateSegmentSpend(blocks, utxo_snapshot, script_check_plans);
}

BlockSpendResult<SegmentSpendValidation> ValidateSegmentSpend(
    std::span<const SegmentBlockView> blocks,
    const SegmentUtxoSnapshotBackend& utxo_snapshot,
    ScriptCheckPlanCollection script_check_plans)
{
    const SegmentSpendInputPlan input_plan{PlanSegmentSpendInputs(blocks)};
    const auto overwrite_check{CheckSegmentNoUnspentOutputOverwrite(blocks, input_plan, utxo_snapshot)};
    if (!overwrite_check) {
        return Unexpected<BlockSpendError>{overwrite_check.error()};
    }

    SegmentSpentOutputJoin joined{JoinSegmentSpentOutputs(blocks, utxo_snapshot)};
    if (joined.status != SegmentSpentOutputJoinStatus::Complete) {
        if (joined.status == SegmentSpentOutputJoinStatus::BackendMismatch) {
            return Unexpected<BlockSpendError>{SegmentBackendMismatch()};
        }
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
    SegmentAccumulator& accumulator)
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
