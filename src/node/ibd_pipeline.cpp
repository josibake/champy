// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/ibd_pipeline.h>

#include <cassert>
#include <utility>

namespace node {

void IbdStageMetrics::Record(std::chrono::nanoseconds duration, uint64_t bytes_processed) noexcept
{
    ++blocks;
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

void IbdPipelineMetrics::Record(IbdPipelineStage stage, std::chrono::nanoseconds duration, uint64_t bytes_processed) noexcept
{
    Stage(stage).Record(duration, bytes_processed);
}

IbdOrderedRetireQueue::IbdOrderedRetireQueue(int next_height) : m_next_height{next_height}
{
    assert(next_height >= 0);
}

IbdRetireResult IbdOrderedRetireQueue::Add(PeerBlockRef block)
{
    if (block.height < 0) {
        return {.status = IbdRetireStatus::InvalidHeight};
    }
    if (block.height < m_next_height) {
        return {.status = IbdRetireStatus::StaleHeight};
    }
    if (m_pending.contains(block.height)) {
        return {.status = IbdRetireStatus::DuplicateHeight};
    }

    const std::size_t previous_ready{m_ready.size()};
    m_pending.emplace(block.height, std::move(block));
    MoveContiguousToReady();
    const std::size_t new_ready{m_ready.size() - previous_ready};

    return {
        .status = new_ready > 0 ? IbdRetireStatus::Ready : IbdRetireStatus::Queued,
        .ready_count = new_ready,
    };
}

std::vector<PeerBlockRef> IbdOrderedRetireQueue::PopReady()
{
    std::vector<PeerBlockRef> ready;
    ready.swap(m_ready);
    return ready;
}

void IbdOrderedRetireQueue::MoveContiguousToReady()
{
    while (true) {
        auto it{m_pending.find(m_next_height)};
        if (it == m_pending.end()) return;

        m_ready.push_back(std::move(it->second));
        m_pending.erase(it);
        ++m_next_height;
    }
}

IbdPipeline::IbdPipeline(int next_commit_height, IbdPipelineLimits limits)
    : m_limits{limits}, m_retire_queue{next_commit_height}
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
    if (m_limits.max_blocks_ahead > 0 &&
        static_cast<std::size_t>(block.height - m_retire_queue.NextHeight()) >= m_limits.max_blocks_ahead) {
        return {.status = IbdAdmissionStatus::TooFarAhead};
    }
    return {.status = IbdAdmissionStatus::Accepted};
}

IbdRetireResult IbdPipeline::MarkValidated(PeerBlockRef block)
{
    return m_retire_queue.Add(std::move(block));
}

std::vector<PeerBlockRef> IbdPipeline::PopReadyToCommit()
{
    return m_retire_queue.PopReady();
}

} // namespace node
