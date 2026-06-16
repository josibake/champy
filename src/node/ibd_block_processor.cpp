// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/ibd_block_processor.h>

#include <chainstate.h>
#include <logging.h>
#include <primitives/block.h>
#include <serialize.h>
#include <validation/chain_validation.h>

#include <cassert>
#include <chrono>
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

void RecordReachedStages(IbdBlockProcessingMetrics& metrics, const NewBlockProcessingResult& validation)
{
    metrics.Record(IbdBlockProcessingStage::StructuralValidation, validation.timings.structural_check);
    if (!validation.BlockAdmissionAttempted()) return;

    metrics.Record(IbdBlockProcessingStage::BlockAdmission, validation.timings.block_acceptance);
    if (!validation.ActivationAttempted()) return;

    metrics.Record(IbdBlockProcessingStage::ContextSnapshot, validation.timings.context_snapshot);
    metrics.Record(IbdBlockProcessingStage::SpendJoin, validation.timings.spend_join, /*bytes_processed=*/0, validation.activated_blocks);
    metrics.Record(IbdBlockProcessingStage::ScriptValidation, validation.timings.script_validation, /*bytes_processed=*/0, validation.activated_blocks);
    metrics.Record(IbdBlockProcessingStage::Commit, validation.timings.activation, /*bytes_processed=*/0, validation.activated_blocks);
}

NewBlockProcessingResult FinishBlockProcessResult(
    NewBlockProcessingResult validation,
    IbdBlockProcessingMetrics& metrics,
    const CBlock& block,
    uint64_t block_bytes)
{
    RecordReachedStages(metrics, validation);

    const IbdBlockProcessingMetrics::Work& work{metrics.WorkMetrics()};
    LogDebug(BCLog::BENCH,
             "IBD direct block hash=%s status=%d new=%d bytes=%u accepted=%u structural=%.2fms accept=%.2fms snapshot=%.2fms spend=%.2fms script=%.2fms commit=%.2fms total=%.2fms\n",
             block.GetHash().ToString(),
             static_cast<int>(validation.status()),
             validation.HasNewStoredBlockData(),
             block_bytes,
             work.accepted_blocks,
             Milliseconds(validation.timings.structural_check),
             Milliseconds(validation.timings.block_acceptance),
             Milliseconds(validation.timings.context_snapshot),
             Milliseconds(validation.timings.spend_join),
             Milliseconds(validation.timings.script_validation),
             Milliseconds(validation.timings.activation),
             Milliseconds(validation.timings.total));

    return validation;
}

} // namespace

IbdBlockDownloadWindow BuildIbdBlockDownloadWindow(
    std::optional<validation::ActiveChainTipSnapshot> active_tip,
    IbdBlockDownloadLimits limits)
{
    if (!active_tip) {
        return {
            .next_commit_height = 0,
            .expected_parent_hash = std::nullopt,
            .limits = limits,
        };
    }
    return {
        .next_commit_height = active_tip->height + 1,
        .expected_parent_hash = active_tip->hash,
        .limits = limits,
    };
}

IbdBlockProcessor::IbdBlockProcessor(ChainstateManager& chainman)
    : m_chainman{chainman}
{
}

IbdBlockDownloadWindow IbdBlockProcessor::AdmissionWindow(IbdBlockDownloadLimits limits) const
{
    AssertLockNotHeld(::cs_main);
    return BuildIbdBlockDownloadWindow(m_chainman.ActiveTipSnapshot(), limits);
}

NewBlockProcessingResult IbdBlockProcessor::ProcessDownloadedBlock(IbdBlockProcessRequest request)
{
    AssertLockNotHeld(::cs_main);
    assert(request.block);

    const uint64_t block_bytes{SerializedBlockBytes(*request.block)};
    m_metrics.Record(IbdBlockProcessingStage::Download, /*duration=*/std::chrono::nanoseconds{0}, block_bytes);

    NewBlockProcessingResult validation{::ProcessNewBlock({
        .chainman = m_chainman,
        .chain_events = request.chain_events,
        .block = request.block,
        .options = {
            .block_data_storage = request.block_data_storage,
            .header = {.min_pow_checked = request.min_pow_checked},
        },
        .time = request.time,
    })};
    if (validation.candidate_context()) {
        m_metrics.RecordAcceptedBlock();
    }

    return FinishBlockProcessResult(
        std::move(validation),
        m_metrics,
        *request.block,
        block_bytes);
}

} // namespace node
