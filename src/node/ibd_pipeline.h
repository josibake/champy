// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_IBD_PIPELINE_H
#define BITCOIN_NODE_IBD_PIPELINE_H

#include <consensus/block_commit.h>
#include <consensus/block_spend.h>
#include <node/block_download_types.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class CBlock;

namespace node {

enum class IbdPipelineStage {
    Download,
    StructuralValidation,
    BlockAdmission,
    ContextSnapshot,
    SpendJoin,
    ScriptValidation,
    Commit,
};

struct IbdStageMetrics {
    uint64_t blocks{0};
    uint64_t bytes{0};
    std::chrono::nanoseconds elapsed{0};

    void Record(std::chrono::nanoseconds duration, uint64_t bytes_processed = 0, uint64_t blocks_processed = 1) noexcept;
};

class IbdPipelineMetrics
{
public:
    void Record(IbdPipelineStage stage, std::chrono::nanoseconds duration, uint64_t bytes_processed = 0, uint64_t blocks_processed = 1) noexcept;

    [[nodiscard]] const IbdStageMetrics& Stage(IbdPipelineStage stage) const noexcept;
    [[nodiscard]] IbdStageMetrics& Stage(IbdPipelineStage stage) noexcept;

private:
    IbdStageMetrics m_download;
    IbdStageMetrics m_structural_validation;
    IbdStageMetrics m_block_admission;
    IbdStageMetrics m_context_snapshot;
    IbdStageMetrics m_spend_join;
    IbdStageMetrics m_script_validation;
    IbdStageMetrics m_commit;
};

enum class IbdRetireStatus {
    Ready,
    Queued,
    NotReady,
    ParentMismatch,
    DuplicateHeight,
    StaleHeight,
    InvalidHeight,
};

struct IbdRetireResult {
    IbdRetireStatus status{IbdRetireStatus::InvalidHeight};
    std::size_t ready_count{0};
};

enum class IbdScriptValidationStatus {
    NotSubmitted,
    Pending,
    Valid,
    Failed,
};

struct IbdBlockCommitWork {
    std::shared_ptr<const CBlock> block_data;
    Consensus::BlockCommitContext commit_context;
    Consensus::BlockSpendEffects spend_effects;
};

struct IbdValidatedBlockPackage {
    PeerBlockRef block;
    uint256 parent_hash{};
    std::optional<IbdBlockCommitWork> commit_work{};
    IbdScriptValidationStatus script_status{IbdScriptValidationStatus::NotSubmitted};

    [[nodiscard]] bool ReadyForSerializedCommit() const noexcept
    {
        return commit_work.has_value() && script_status == IbdScriptValidationStatus::Valid;
    }
};

[[nodiscard]] IbdValidatedBlockPackage CommitReadyPackage(PeerBlockRef block);

struct IbdRetireChainPosition {
    int next_height{-1};
    std::optional<uint256> expected_parent_hash{};
};

template <typename Package>
class IbdOrderedPackageRetireQueue
{
public:
    explicit IbdOrderedPackageRetireQueue(int next_height)
        : IbdOrderedPackageRetireQueue{IbdRetireChainPosition{.next_height = next_height}}
    {
    }

    explicit IbdOrderedPackageRetireQueue(IbdRetireChainPosition position)
        : m_next_height{position.next_height},
          m_expected_parent_hash{position.expected_parent_hash}
    {
        assert(position.next_height >= 0);
    }

    [[nodiscard]] IbdRetireResult Add(Package package)
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

    [[nodiscard]] std::vector<Package> PopReady()
    {
        std::vector<Package> ready;
        ready.swap(m_ready);
        return ready;
    }

    [[nodiscard]] int NextHeight() const noexcept { return m_next_height; }
    [[nodiscard]] const std::optional<uint256>& ExpectedParentHash() const noexcept { return m_expected_parent_hash; }
    [[nodiscard]] std::size_t PendingCount() const noexcept { return m_pending.size(); }
    [[nodiscard]] std::size_t ReadyCount() const noexcept { return m_ready.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_pending.empty() && m_ready.empty(); }

private:
    [[nodiscard]] bool ParentChainMatches(const Package& package) const
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

    void MoveContiguousToReady()
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

    int m_next_height;
    std::optional<uint256> m_expected_parent_hash;
    std::map<int, Package> m_pending;
    std::vector<Package> m_ready;
};

/**
 * Orders validated IBD blocks before the serialized commit stage.
 *
 * The queue is scoped to one candidate chain. It intentionally keys by height:
 * forks are resolved before work enters this queue, and commit must retire a
 * contiguous height range.
 */
class IbdOrderedRetireQueue
{
public:
    explicit IbdOrderedRetireQueue(int next_height);
    explicit IbdOrderedRetireQueue(IbdRetireChainPosition position);

    [[nodiscard]] IbdRetireResult Add(PeerBlockRef block);
    [[nodiscard]] IbdRetireResult Add(IbdValidatedBlockPackage package);
    [[nodiscard]] std::vector<IbdValidatedBlockPackage> PopReady();

    [[nodiscard]] int NextHeight() const noexcept { return m_queue.NextHeight(); }
    [[nodiscard]] const std::optional<uint256>& ExpectedParentHash() const noexcept { return m_queue.ExpectedParentHash(); }
    [[nodiscard]] std::size_t PendingCount() const noexcept { return m_queue.PendingCount(); }
    [[nodiscard]] std::size_t ReadyCount() const noexcept { return m_queue.ReadyCount(); }
    [[nodiscard]] bool Empty() const noexcept { return m_queue.Empty(); }

private:
    IbdOrderedPackageRetireQueue<IbdValidatedBlockPackage> m_queue;
};

struct IbdPipelineLimits {
    std::size_t max_blocks_ahead{32};
};

struct IbdPipelineAdmissionWindow {
    int next_commit_height{-1};
    std::optional<uint256> expected_parent_hash{};
    IbdPipelineLimits limits{};
};

enum class IbdAdmissionStatus {
    Accepted,
    ParentMismatch,
    StaleHeight,
    TooFarAhead,
    InvalidHeight,
};

struct IbdAdmissionResult {
    IbdAdmissionStatus status{IbdAdmissionStatus::InvalidHeight};
};

class IbdPipeline
{
public:
    explicit IbdPipeline(int next_commit_height, IbdPipelineLimits limits = {});
    explicit IbdPipeline(IbdRetireChainPosition position, IbdPipelineLimits limits = {});

    [[nodiscard]] IbdAdmissionResult Admit(PeerBlockRef block) const noexcept;
    [[nodiscard]] IbdRetireResult MarkValidated(PeerBlockRef block);
    [[nodiscard]] IbdRetireResult MarkValidated(IbdValidatedBlockPackage package);
    [[nodiscard]] std::vector<IbdValidatedBlockPackage> PopReadyToCommit();

    [[nodiscard]] int NextCommitHeight() const noexcept { return m_retire_queue.NextHeight(); }
    [[nodiscard]] std::size_t PendingRetireCount() const noexcept { return m_retire_queue.PendingCount(); }
    [[nodiscard]] std::size_t ReadyCommitCount() const noexcept { return m_retire_queue.ReadyCount(); }

    [[nodiscard]] IbdPipelineMetrics& Metrics() noexcept { return m_metrics; }
    [[nodiscard]] const IbdPipelineMetrics& Metrics() const noexcept { return m_metrics; }

private:
    IbdPipelineLimits m_limits;
    IbdOrderedRetireQueue m_retire_queue;
    IbdPipelineMetrics m_metrics;
};

} // namespace node

#endif // BITCOIN_NODE_IBD_PIPELINE_H
