// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/ibd_block_processor.h>

#include <logging.h>
#include <primitives/block.h>
#include <serialize.h>
#include <validation/chain_validation.h>

#include <cassert>
#include <chrono>
#include <optional>
#include <utility>

namespace node {
namespace {

uint64_t SerializedBlockBytes(const CBlock& block)
{
    return static_cast<uint64_t>(::GetSerializeSize(TX_WITH_WITNESS(block)));
}

double Milliseconds(std::chrono::nanoseconds duration)
{
    return std::chrono::duration<double, std::milli>{duration}.count();
}

PeerBlockRef PeerBlockRefFor(const ChainWorkBlockSnapshot& block)
{
    return {
        .hash = block.hash,
        .parent_hash = block.parent_hash,
        .height = block.height,
        .chain_work = block.chain_work,
    };
}

IbdAcceptedBlockCandidate MakeAcceptedBlockCandidate(
    const NewBlockCandidateContextSnapshot& context,
    std::shared_ptr<const CBlock> block)
{
    return {
        .block = PeerBlockRefFor(context.block),
        .block_data = std::move(block),
        .context = context,
    };
}

void FinishNewBlockTiming(NewBlockProcessingResult& result, std::chrono::steady_clock::time_point start) noexcept
{
    result.timings.total = std::chrono::steady_clock::now() - start;
}

IbdBlockProcessResult FinishBlockProcessResult(
    NewBlockProcessingResult validation,
    IbdPipelineMetrics& metrics,
    std::chrono::steady_clock::time_point total_start,
    const CBlock& block,
    uint64_t block_bytes,
    std::optional<IbdAcceptedBlockCandidate> accepted_candidate = std::nullopt)
{
    FinishNewBlockTiming(validation, total_start);
    metrics.Record(IbdPipelineStage::StructuralValidation, validation.timings.structural_check);
    if (validation.status != NewBlockProcessingStatus::BlockCheckFailed) {
        metrics.Record(IbdPipelineStage::BlockAdmission, validation.timings.block_acceptance);
    }
    if (validation.status == NewBlockProcessingStatus::ActivationFailed ||
        validation.status == NewBlockProcessingStatus::Processed) {
        metrics.Record(IbdPipelineStage::ContextSnapshot, validation.timings.context_snapshot);
        metrics.Record(IbdPipelineStage::SpendJoin, validation.timings.spend_join, /*bytes_processed=*/0, validation.activated_blocks);
        metrics.Record(IbdPipelineStage::ScriptValidation, validation.timings.script_validation, /*bytes_processed=*/0, validation.activated_blocks);
        metrics.Record(IbdPipelineStage::Commit, validation.timings.activation, /*bytes_processed=*/0, validation.activated_blocks);
    }

    LogDebug(BCLog::BENCH,
             "IBD pipeline block hash=%s status=%d new=%d bytes=%u structural=%.2fms accept=%.2fms snapshot=%.2fms spend=%.2fms script=%.2fms activate=%.2fms total=%.2fms\n",
             block.GetHash().ToString(),
             static_cast<int>(validation.status),
             validation.new_block(),
             block_bytes,
             Milliseconds(validation.timings.structural_check),
             Milliseconds(validation.timings.block_acceptance),
             Milliseconds(validation.timings.context_snapshot),
             Milliseconds(validation.timings.spend_join),
             Milliseconds(validation.timings.script_validation),
             Milliseconds(validation.timings.activation),
             Milliseconds(validation.timings.total));

    return {
        .validation = std::move(validation),
        .accepted_candidate = std::move(accepted_candidate),
    };
}

} // namespace

IbdBlockProcessResult IbdBlockProcessor::ProcessDownloadedBlock(IbdBlockProcessRequest request)
{
    AssertLockNotHeld(::cs_main);
    assert(request.block);

    const uint64_t block_bytes{SerializedBlockBytes(*request.block)};
    m_metrics.Record(IbdPipelineStage::Download, /*duration=*/std::chrono::nanoseconds{0}, block_bytes);

    ChainValidationService chain_validation{m_chainman};
    BlockValidationState state;
    NewBlockProcessingResult validation;
    const auto total_start{std::chrono::steady_clock::now()};

    const auto structural_start{std::chrono::steady_clock::now()};
    const NewBlockStructuralCheckResult structural_check{chain_validation.CheckNewBlockStructural(request.block, state)};
    validation.timings.structural_check = std::chrono::steady_clock::now() - structural_start;
    if (!structural_check.passed()) {
        chain_validation.ReportBlockChecked(request.block, state);
        LogError("%s: AcceptBlock FAILED (%s)\n", __func__, state.ToString());
        return FinishBlockProcessResult(std::move(validation), m_metrics, total_start, *request.block, block_bytes);
    }

    const auto accept_start{std::chrono::steady_clock::now()};
    const BlockAcceptanceResult acceptance{chain_validation.AcceptNewBlockData(
        request.block,
        state,
        {
            .block_data_storage = request.block_data_storage,
            .header = {.min_pow_checked = request.min_pow_checked},
            .structural_check = structural_check.proof,
        },
        request.time)};
    validation.timings.block_acceptance = std::chrono::steady_clock::now() - accept_start;
    validation.block_acceptance_status = acceptance.status;
    if (!acceptance.accepted_for_processing()) {
        validation.status = NewBlockProcessingStatus::BlockNotAccepted;
        chain_validation.ReportBlockChecked(request.block, state);
        LogError("%s: AcceptBlock FAILED (%s)\n", __func__, state.ToString());
        return FinishBlockProcessResult(std::move(validation), m_metrics, total_start, *request.block, block_bytes);
    }

    validation.status = NewBlockProcessingStatus::ActivationFailed;
    const auto snapshot_start{std::chrono::steady_clock::now()};
    validation.candidate_context = chain_validation.SnapshotAcceptedBlockContext(request.block->GetHash());
    validation.timings.context_snapshot = std::chrono::steady_clock::now() - snapshot_start;
    std::optional<IbdAcceptedBlockCandidate> accepted_candidate;
    if (validation.candidate_context) {
        accepted_candidate = MakeAcceptedBlockCandidate(*validation.candidate_context, request.block);
    }

    BlockValidationState activate_state;
    const auto activation_start{std::chrono::steady_clock::now()};
    const BlockActivationResult activation{chain_validation.ActivateAcceptedBlock(request.chain_events, request.block, activate_state)};
    validation.timings.activation = std::chrono::steady_clock::now() - activation_start;
    validation.timings.spend_join = activation.timings.spend_join;
    validation.timings.script_validation = activation.timings.script_validation;
    validation.activated_blocks = activation.connected_blocks;
    if (!activation.Succeeded()) {
        LogError("%s: ActivateBestChain failed (%s)\n", __func__, activate_state.ToString());
        return FinishBlockProcessResult(std::move(validation), m_metrics, total_start, *request.block, block_bytes, std::move(accepted_candidate));
    }

    validation.status = NewBlockProcessingStatus::Processed;
    return FinishBlockProcessResult(std::move(validation), m_metrics, total_start, *request.block, block_bytes, std::move(accepted_candidate));
}

} // namespace node
