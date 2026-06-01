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
        .block_data = nullptr,
        .spend_effects_ready = true,
        .script_status = IbdScriptValidationStatus::Valid,
    };
}

IbdOrderedRetireQueue::IbdOrderedRetireQueue(int next_height)
    : IbdOrderedRetireQueue{IbdRetireChainPosition{.next_height = next_height}}
{
}

IbdOrderedRetireQueue::IbdOrderedRetireQueue(IbdRetireChainPosition position)
    : m_next_height{position.next_height},
      m_expected_parent_hash{position.expected_parent_hash}
{
    assert(position.next_height >= 0);
}

IbdRetireResult IbdOrderedRetireQueue::Add(PeerBlockRef block)
{
    return Add(CommitReadyPackage(std::move(block)));
}

IbdRetireResult IbdOrderedRetireQueue::Add(IbdValidatedBlockPackage package)
{
    if (!package.ReadyForSerializedCommit()) {
        return {.status = IbdRetireStatus::NotReady};
    }
    if (package.block.height < 0) {
        return {.status = IbdRetireStatus::InvalidHeight};
    }
    if (package.block.height < m_next_height) {
        return {.status = IbdRetireStatus::StaleHeight};
    }
    if (m_pending.contains(package.block.height)) {
        return {.status = IbdRetireStatus::DuplicateHeight};
    }
    if (!ParentChainMatches(package)) {
        return {.status = IbdRetireStatus::ParentMismatch};
    }

    const std::size_t previous_ready{m_ready.size()};
    m_pending.emplace(package.block.height, std::move(package));
    MoveContiguousToReady();
    const std::size_t new_ready{m_ready.size() - previous_ready};

    return {
        .status = new_ready > 0 ? IbdRetireStatus::Ready : IbdRetireStatus::Queued,
        .ready_count = new_ready,
    };
}

std::vector<IbdValidatedBlockPackage> IbdOrderedRetireQueue::PopReady()
{
    std::vector<IbdValidatedBlockPackage> ready;
    ready.swap(m_ready);
    return ready;
}

bool IbdOrderedRetireQueue::ParentChainMatches(const IbdValidatedBlockPackage& package) const
{
    if (!m_expected_parent_hash) return true;

    if (package.block.height == m_next_height && package.parent_hash != *m_expected_parent_hash) {
        return false;
    }

    const auto previous{m_pending.find(package.block.height - 1)};
    if (previous != m_pending.end() && package.parent_hash != previous->second.block.hash) {
        return false;
    }

    const auto next{m_pending.find(package.block.height + 1)};
    if (next != m_pending.end() && next->second.parent_hash != package.block.hash) {
        return false;
    }

    return true;
}

void IbdOrderedRetireQueue::MoveContiguousToReady()
{
    while (true) {
        auto it{m_pending.find(m_next_height)};
        if (it == m_pending.end()) return;

        if (m_expected_parent_hash) {
            assert(it->second.parent_hash == *m_expected_parent_hash);
            m_expected_parent_hash = it->second.block.hash;
        }
        m_ready.push_back(std::move(it->second));
        m_pending.erase(it);
        ++m_next_height;
    }
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
