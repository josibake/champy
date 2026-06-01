// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_IBD_BLOCK_PROCESSOR_H
#define BITCOIN_NODE_IBD_BLOCK_PROCESSOR_H

#include <kernel/cs_main.h>
#include <consensus/segment_spend.h>
#include <node/ibd_pipeline.h>
#include <validation/block_data_admission.h>
#include <validation/block_validation.h>

#include <memory>
#include <optional>
#include <vector>

class ChainstateEventSink;
class ChainstateManager;
class CBlock;

namespace node {

struct IbdBlockProcessRequest {
    ChainstateEventSink* chain_events{nullptr};
    std::shared_ptr<const CBlock> block;
    BlockDataStorageMode block_data_storage{BlockDataStorageMode::ApplyAdmissionChecks};
    bool min_pow_checked{false};
    BlockValidationTime time{};
};

struct IbdAcceptedBlockCandidate {
    PeerBlockRef block;
    std::shared_ptr<const CBlock> block_data;
    NewBlockCandidateContextSnapshot context;
};

struct IbdBlockProcessResult {
    NewBlockProcessingResult validation;
    std::optional<IbdAcceptedBlockCandidate> accepted_candidate{};
};

struct IbdCandidateBlockValidation {
    IbdValidatedBlockPackage package;
    std::vector<Consensus::TransactionScriptCheckPlan> script_checks;
};

struct IbdCandidateSegmentValidation {
    std::vector<IbdCandidateBlockValidation> blocks;
    Consensus::SegmentSpendSummary spend_summary;
};

[[nodiscard]] bool IbdCandidateMatchesContext(const IbdAcceptedBlockCandidate& candidate);
[[nodiscard]] Consensus::SegmentBlockContext BuildIbdSegmentBlockContext(const NewBlockCandidateContextSnapshot& context);
[[nodiscard]] Consensus::SegmentBlockView BuildIbdSegmentBlockView(const IbdAcceptedBlockCandidate& candidate);
[[nodiscard]] Consensus::BlockCommitContext BuildIbdBlockCommitContext(const NewBlockCandidateContextSnapshot& context);
[[nodiscard]] IbdValidatedBlockPackage MakeIbdValidatedBlockPackage(
    IbdAcceptedBlockCandidate candidate,
    Consensus::BlockSpendEffects spend_effects,
    IbdScriptValidationStatus script_status);
[[nodiscard]] Consensus::BlockSpendResult<IbdCandidateSegmentValidation> ValidateIbdCandidateSegment(
    std::vector<IbdAcceptedBlockCandidate> candidates,
    const Consensus::SegmentSpendBatchView& spend_state,
    Consensus::ScriptCheckPlanCollection script_check_plans);

/**
 * Node-owned IBD execution shell.
 *
 * This currently runs on the caller thread to preserve behavior. It owns the
 * node-side metrics and is the narrow place where later queued or parallel IBD
 * execution can replace the inline call.
 */
class IbdBlockProcessor
{
public:
    explicit IbdBlockProcessor(ChainstateManager& chainman) : m_chainman{chainman} {}

    [[nodiscard]] IbdPipelineAdmissionWindow AdmissionWindow(IbdPipelineLimits limits) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] IbdBlockProcessResult ProcessDownloadedBlock(IbdBlockProcessRequest request) LOCKS_EXCLUDED(::cs_main);

    [[nodiscard]] const IbdPipelineMetrics& Metrics() const noexcept { return m_metrics; }

private:
    ChainstateManager& m_chainman;
    IbdPipelineMetrics m_metrics;
};

} // namespace node

#endif // BITCOIN_NODE_IBD_BLOCK_PROCESSOR_H
