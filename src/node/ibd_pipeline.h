// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_IBD_PIPELINE_H
#define BITCOIN_NODE_IBD_PIPELINE_H

#include <node/block_download_types.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

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

    void Record(std::chrono::nanoseconds duration, uint64_t bytes_processed = 0) noexcept;
};

class IbdPipelineMetrics
{
public:
    void Record(IbdPipelineStage stage, std::chrono::nanoseconds duration, uint64_t bytes_processed = 0) noexcept;

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
    DuplicateHeight,
    StaleHeight,
    InvalidHeight,
};

struct IbdRetireResult {
    IbdRetireStatus status{IbdRetireStatus::InvalidHeight};
    std::size_t ready_count{0};
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

    [[nodiscard]] IbdRetireResult Add(PeerBlockRef block);
    [[nodiscard]] std::vector<PeerBlockRef> PopReady();

    [[nodiscard]] int NextHeight() const noexcept { return m_next_height; }
    [[nodiscard]] std::size_t PendingCount() const noexcept { return m_pending.size(); }
    [[nodiscard]] std::size_t ReadyCount() const noexcept { return m_ready.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_pending.empty() && m_ready.empty(); }

private:
    void MoveContiguousToReady();

    int m_next_height;
    std::map<int, PeerBlockRef> m_pending;
    std::vector<PeerBlockRef> m_ready;
};

struct IbdPipelineLimits {
    std::size_t max_blocks_ahead{32};
};

struct IbdPipelineAdmissionWindow {
    int next_commit_height{-1};
    IbdPipelineLimits limits{};
};

enum class IbdAdmissionStatus {
    Accepted,
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

    [[nodiscard]] IbdAdmissionResult Admit(PeerBlockRef block) const noexcept;
    [[nodiscard]] IbdRetireResult MarkValidated(PeerBlockRef block);
    [[nodiscard]] std::vector<PeerBlockRef> PopReadyToCommit();

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
