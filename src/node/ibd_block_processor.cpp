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

} // namespace

IbdBlockProcessResult IbdBlockProcessor::ProcessDownloadedBlock(IbdBlockProcessRequest request)
{
    AssertLockNotHeld(::cs_main);
    assert(request.block);

    const uint64_t block_bytes{SerializedBlockBytes(*request.block)};
    m_metrics.Record(IbdPipelineStage::Download, /*duration=*/std::chrono::nanoseconds{0}, block_bytes);

    ChainValidationService chain_validation{m_chainman};
    BlockValidationState structural_state;
    const auto structural_start{std::chrono::steady_clock::now()};
    const NewBlockStructuralCheckResult structural_check{chain_validation.CheckNewBlockStructural(request.block, structural_state)};
    const auto structural_elapsed{std::chrono::steady_clock::now() - structural_start};

    const NewBlockProcessingResult validation{chain_validation.ProcessNewBlock(
        request.chain_events,
        request.block,
        {
            .block_data_storage = request.block_data_storage,
            .header = {.min_pow_checked = request.min_pow_checked},
            .structural_check = structural_check.proof,
        },
        request.time)};

    m_metrics.Record(IbdPipelineStage::StructuralValidation, structural_elapsed);
    m_metrics.Record(IbdPipelineStage::BlockAdmission, validation.timings.block_acceptance);
    m_metrics.Record(IbdPipelineStage::ContextSnapshot, validation.timings.context_snapshot);
    m_metrics.Record(IbdPipelineStage::Commit, validation.timings.activation);

    LogDebug(BCLog::BENCH,
             "IBD pipeline block hash=%s status=%d new=%d bytes=%u structural=%.2fms accept=%.2fms snapshot=%.2fms activate=%.2fms total=%.2fms\n",
             request.block->GetHash().ToString(),
             static_cast<int>(validation.status),
             validation.new_block(),
             block_bytes,
             Milliseconds(structural_elapsed),
             Milliseconds(validation.timings.block_acceptance),
             Milliseconds(validation.timings.context_snapshot),
             Milliseconds(validation.timings.activation),
             Milliseconds(validation.timings.total));

    return {.validation = validation};
}

} // namespace node
