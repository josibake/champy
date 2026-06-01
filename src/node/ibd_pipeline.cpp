// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/ibd_pipeline.h>

#include <cassert>
#include <utility>

namespace node {

void IbdStageMetrics::Record(std::chrono::nanoseconds duration, uint64_t bytes_processed, uint64_t blocks_processed) noexcept
{
    blocks += blocks_processed;
    bytes += bytes_processed;
    elapsed += duration;
}

IbdStageMetrics& IbdPipelineMetrics::Stage(IbdPipelineStage stage) noexcept
{
    switch (stage) {
    case IbdPipelineStage::Download:
        return m_download;
    case IbdPipelineStage::StructuralValidation:
        return m_structural_validation;
    case IbdPipelineStage::BlockAdmission:
        return m_block_admission;
    case IbdPipelineStage::ContextSnapshot:
        return m_context_snapshot;
    case IbdPipelineStage::SpendJoin:
        return m_spend_join;
    case IbdPipelineStage::ScriptValidation:
        return m_script_validation;
    case IbdPipelineStage::Commit:
        return m_commit;
    }
    assert(false);
    return m_commit;
}

const IbdStageMetrics& IbdPipelineMetrics::Stage(IbdPipelineStage stage) const noexcept
{
    return const_cast<IbdPipelineMetrics&>(*this).Stage(stage);
}

void IbdPipelineMetrics::Record(IbdPipelineStage stage, std::chrono::nanoseconds duration, uint64_t bytes_processed, uint64_t blocks_processed) noexcept
{
    Stage(stage).Record(duration, bytes_processed, blocks_processed);
}

IbdValidatedBlockPackage CommitReadyPackage(PeerBlockRef block)
{
    const uint256 parent_hash{block.parent_hash};
    return IbdValidatedBlockPackage{
        .block = std::move(block),
        .parent_hash = parent_hash,
        .commit_work = IbdBlockCommitWork{},
        .script_status = IbdScriptValidationStatus::Valid,
    };
}

IbdOrderedRetireQueue::IbdOrderedRetireQueue(int next_height)
    : IbdOrderedRetireQueue{IbdRetireChainPosition{.next_height = next_height}}
{
}

IbdOrderedRetireQueue::IbdOrderedRetireQueue(IbdRetireChainPosition position)
    : m_queue{std::move(position)}
{
}

IbdRetireResult IbdOrderedRetireQueue::Add(PeerBlockRef block)
{
    return Add(CommitReadyPackage(std::move(block)));
}

IbdRetireResult IbdOrderedRetireQueue::Add(IbdValidatedBlockPackage package)
{
    return m_queue.Add(std::move(package));
}

std::vector<IbdValidatedBlockPackage> IbdOrderedRetireQueue::PopReady()
{
    return m_queue.PopReady();
}

IbdPipeline::IbdPipeline(int next_commit_height, IbdPipelineLimits limits)
    : IbdPipeline{IbdRetireChainPosition{.next_height = next_commit_height}, limits}
{
}

IbdPipeline::IbdPipeline(IbdRetireChainPosition position, IbdPipelineLimits limits)
    : m_limits{limits}, m_retire_queue{std::move(position)}
{
}

IbdAdmissionResult IbdPipeline::Admit(PeerBlockRef block) const noexcept
{
    if (block.height < 0) {
        return {.status = IbdAdmissionStatus::InvalidHeight};
    }
    if (block.height < m_retire_queue.NextHeight()) {
        return {.status = IbdAdmissionStatus::StaleHeight};
    }
    if (block.height == m_retire_queue.NextHeight()) {
        const std::optional<uint256>& expected_parent_hash{m_retire_queue.ExpectedParentHash()};
        if (expected_parent_hash && block.parent_hash != *expected_parent_hash) {
            return {.status = IbdAdmissionStatus::ParentMismatch};
        }
    }
    if (m_limits.max_blocks_ahead > 0 &&
        static_cast<std::size_t>(block.height - m_retire_queue.NextHeight()) >= m_limits.max_blocks_ahead) {
        return {.status = IbdAdmissionStatus::TooFarAhead};
    }
    return {.status = IbdAdmissionStatus::Accepted};
}

IbdRetireResult IbdPipeline::MarkValidated(PeerBlockRef block)
{
    return MarkValidated(CommitReadyPackage(std::move(block)));
}

IbdRetireResult IbdPipeline::MarkValidated(IbdValidatedBlockPackage package)
{
    return m_retire_queue.Add(std::move(package));
}

std::vector<IbdValidatedBlockPackage> IbdPipeline::PopReadyToCommit()
{
    return m_retire_queue.PopReady();
}

} // namespace node
