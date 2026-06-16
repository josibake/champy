// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_IBD_BLOCK_DOWNLOAD_H
#define BITCOIN_NODE_IBD_BLOCK_DOWNLOAD_H

#include <node/block_download_types.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace node {

struct IbdBlockDownloadLimits {
    std::size_t max_blocks_ahead{32};
};

struct IbdBlockDownloadWindow {
    int next_commit_height{-1};
    std::optional<uint256> expected_parent_hash{};
    IbdBlockDownloadLimits limits{};
};

enum class IbdBlockDownloadAdmissionStatus {
    Accepted,
    ParentMismatch,
    StaleHeight,
    TooFarAhead,
    InvalidHeight,
};

struct IbdBlockDownloadAdmissionResult {
    IbdBlockDownloadAdmissionStatus status{IbdBlockDownloadAdmissionStatus::InvalidHeight};
};

[[nodiscard]] inline IbdBlockDownloadAdmissionResult AdmitIbdBlockDownloadCandidate(
    const IbdBlockDownloadWindow& window,
    PeerBlockRef block) noexcept
{
    if (block.height < 0) {
        return {.status = IbdBlockDownloadAdmissionStatus::InvalidHeight};
    }
    if (block.height < window.next_commit_height) {
        return {.status = IbdBlockDownloadAdmissionStatus::StaleHeight};
    }
    if (block.height == window.next_commit_height &&
        window.expected_parent_hash &&
        block.parent_hash != *window.expected_parent_hash) {
        return {.status = IbdBlockDownloadAdmissionStatus::ParentMismatch};
    }
    if (window.limits.max_blocks_ahead > 0 &&
        static_cast<std::size_t>(block.height - window.next_commit_height) >= window.limits.max_blocks_ahead) {
        return {.status = IbdBlockDownloadAdmissionStatus::TooFarAhead};
    }
    return {.status = IbdBlockDownloadAdmissionStatus::Accepted};
}

enum class IbdBlockProcessingStage {
    Download,
    StructuralValidation,
    BlockAdmission,
    ContextSnapshot,
    SpendJoin,
    ScriptValidation,
    Commit,
};

struct IbdBlockProcessingStageMetrics {
    uint64_t blocks{0};
    uint64_t bytes{0};
    std::chrono::nanoseconds elapsed{0};

    void Record(std::chrono::nanoseconds duration, uint64_t bytes_processed = 0, uint64_t blocks_processed = 1) noexcept
    {
        blocks += blocks_processed;
        bytes += bytes_processed;
        elapsed += duration;
    }
};

class IbdBlockProcessingMetrics
{
public:
    void Record(IbdBlockProcessingStage stage, std::chrono::nanoseconds duration, uint64_t bytes_processed = 0, uint64_t blocks_processed = 1) noexcept
    {
        Stage(stage).Record(duration, bytes_processed, blocks_processed);
    }
    void RecordAcceptedBlock() noexcept { ++m_work.accepted_blocks; }

    [[nodiscard]] const IbdBlockProcessingStageMetrics& Stage(IbdBlockProcessingStage stage) const noexcept
    {
        return const_cast<IbdBlockProcessingMetrics&>(*this).Stage(stage);
    }
    [[nodiscard]] IbdBlockProcessingStageMetrics& Stage(IbdBlockProcessingStage stage) noexcept
    {
        switch (stage) {
        case IbdBlockProcessingStage::Download:
            return m_download;
        case IbdBlockProcessingStage::StructuralValidation:
            return m_structural_validation;
        case IbdBlockProcessingStage::BlockAdmission:
            return m_block_admission;
        case IbdBlockProcessingStage::ContextSnapshot:
            return m_context_snapshot;
        case IbdBlockProcessingStage::SpendJoin:
            return m_spend_join;
        case IbdBlockProcessingStage::ScriptValidation:
            return m_script_validation;
        case IbdBlockProcessingStage::Commit:
            return m_commit;
        }
        assert(false);
        return m_commit;
    }

    struct Work {
        uint64_t accepted_blocks{0};
    };

    [[nodiscard]] const Work& WorkMetrics() const noexcept { return m_work; }

private:
    IbdBlockProcessingStageMetrics m_download;
    IbdBlockProcessingStageMetrics m_structural_validation;
    IbdBlockProcessingStageMetrics m_block_admission;
    IbdBlockProcessingStageMetrics m_context_snapshot;
    IbdBlockProcessingStageMetrics m_spend_join;
    IbdBlockProcessingStageMetrics m_script_validation;
    IbdBlockProcessingStageMetrics m_commit;
    Work m_work;
};

} // namespace node

#endif // BITCOIN_NODE_IBD_BLOCK_DOWNLOAD_H
