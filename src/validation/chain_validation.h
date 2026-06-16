// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAIN_VALIDATION_H
#define BITCOIN_CHAIN_VALIDATION_H

#include <kernel/cs_main.h>
#include <validation/block_validation.h>
#include <validation/core_chain_activation.h>

#include <memory>
#include <span>

class ChainstateEventSink;
class ChainstateManager;

struct ProcessNewBlockHeadersRequest {
    ChainstateManager& chainman;
    std::span<const CBlockHeader> headers;
    BlockHeaderAcceptanceOptions options{};
    BlockValidationTime time;
    BlockValidationState& state;
};

struct CheckNewBlockStructuralRequest {
    ChainstateManager& chainman;
    const std::shared_ptr<const CBlock>& block;
    BlockValidationState& state;
};

struct AcceptBlockRequest {
    ChainstateManager& chainman;
    const std::shared_ptr<const CBlock>& block;
    BlockValidationState& state;
    BlockAcceptanceOptions options{};
    BlockValidationTime time;
};

struct AcceptNewBlockDataRequest {
    ChainstateManager& chainman;
    const std::shared_ptr<const CBlock>& block;
    BlockValidationState& state;
    BlockAcceptanceOptions options{};
    BlockValidationTime time;
};

struct SnapshotAcceptedBlockContextRequest {
    ChainstateManager& chainman;
    const uint256& block_hash;
};

struct PrepareAcceptedTipCommitWorkRequest {
    ChainstateManager& chainman;
    const NewBlockCandidateContextSnapshot& context;
    const std::shared_ptr<const CBlock>& block;
    BlockValidationState& state;
};

struct ActivateAcceptedBlockRequest {
    ChainstateManager& chainman;
    ChainstateEventSink* chain_events{nullptr};
    const std::shared_ptr<const CBlock>& block;
    BlockValidationState& state;
    BlockValidationTime time;
};

struct ActivateAcceptedTipCandidateRequest {
    ChainstateManager& chainman;
    ChainstateEventSink* chain_events{nullptr};
    const std::shared_ptr<const CBlock>& block;
    BlockValidationState& state;
    BlockValidationTime time;
};

struct CommitAcceptedTipCandidateRequest {
    ChainstateManager& chainman;
    ChainstateEventSink* chain_events{nullptr};
    CoreBlockConnectionCommitWork work;
    BlockValidationState& state;
    BlockValidationTime time;
};

struct ReportBlockCheckedRequest {
    ChainstateManager& chainman;
    const std::shared_ptr<const CBlock>& block;
    const BlockValidationState& state;
};

struct ProcessNewBlockRequest {
    ChainstateManager& chainman;
    ChainstateEventSink* chain_events{nullptr};
    const std::shared_ptr<const CBlock>& block;
    NewBlockProcessingOptions options{};
    BlockValidationTime time;
};

struct TestActiveBlockValidityRequest {
    ChainstateManager& chainman;
    const CBlock& block;
    Consensus::BlockCheckOptions options{};
    BlockValidationTime time;
};

[[nodiscard]] NewBlockHeadersResult ProcessNewBlockHeaders(
    ProcessNewBlockHeadersRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockStructuralCheckResult CheckNewBlockStructural(
    CheckNewBlockStructuralRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptBlock(
    AcceptBlockRequest request) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptNewBlockData(
    AcceptNewBlockDataRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] std::optional<NewBlockCandidateContextSnapshot> SnapshotAcceptedBlockContext(
    SnapshotAcceptedBlockContextRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] std::optional<CoreBlockConnectionCommitWork> PrepareAcceptedTipCommitWork(
    PrepareAcceptedTipCommitWorkRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockActivationResult ActivateAcceptedBlock(
    ActivateAcceptedBlockRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockActivationResult ActivateAcceptedTipCandidate(
    ActivateAcceptedTipCandidateRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockActivationResult CommitAcceptedTipCandidate(
    CommitAcceptedTipCandidateRequest request) LOCKS_EXCLUDED(cs_main);
void ReportBlockChecked(ReportBlockCheckedRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(
    ProcessNewBlockRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockValidationState TestActiveBlockValidity(
    TestActiveBlockValidityRequest request) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockValidationState TestActiveBlockValidityLocked(
    TestActiveBlockValidityRequest request) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_CHAIN_VALIDATION_H
