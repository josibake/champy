// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_INTERNAL_H
#define BITCOIN_BLOCK_VALIDATION_INTERNAL_H

#include <kernel/cs_main.h>
#include <validation/block_validation.h>
#include <validation/test_block_validity.h>

#include <memory>
#include <optional>
#include <span>

class BlockHeaderContextProvider;
class BlockIndexLookup;
class Chainstate;
class ChainstateEventSink;
class CoreAcceptedContextReader;
class CoreActivationRuntime;
class CoreBlockDataAdmissionRuntime;
class CoreHeaderAdmissionRuntime;
namespace validation {
class ValidationEventQueue;
} // namespace validation
namespace Consensus {
struct Params;
} // namespace Consensus

[[nodiscard]] NewBlockHeadersResult ProcessNewBlockHeaders(CoreHeaderAdmissionRuntime& runtime, std::span<const CBlockHeader> headers, BlockHeaderAcceptanceOptions options, BlockValidationTime time, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockStructuralCheckResult CheckNewBlockStructural(const Consensus::Params& consensus_params, const std::shared_ptr<const CBlock>& block, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptBlock(CoreBlockDataAdmissionRuntime& runtime, const std::shared_ptr<const CBlock>& pblock, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptNewBlockData(CoreBlockDataAdmissionRuntime& runtime, const std::shared_ptr<const CBlock>& block, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] std::optional<NewBlockCandidateContextSnapshot> SnapshotAcceptedBlockContext(const Consensus::Params& consensus_params, BlockIndexLookup& block_index, const BlockHeaderContextProvider& header_context, const uint256& block_hash) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] std::optional<NewBlockCandidateContextSnapshot> SnapshotAcceptedBlockContext(CoreAcceptedContextReader& reader, const uint256& block_hash) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockActivationResult ActivateAcceptedTipCandidate(CoreActivationRuntime& runtime, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, BlockValidationState& state, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockActivationResult ActivateAcceptedBlock(CoreActivationRuntime& runtime, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, BlockValidationState& state, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
void ReportBlockChecked(validation::ValidationEventQueue& events, const std::shared_ptr<const CBlock>& block, const BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreBlockDataAdmissionRuntime& admission_runtime, CoreAcceptedContextReader& context_reader, CoreActivationRuntime& activation_runtime, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreBlockDataAdmissionRuntime& admission_runtime, CoreAcceptedContextReader& context_reader, CoreActivationRuntime& activation_runtime, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);

BlockValidationState TestBlockValidity(
    Chainstate& chainstate,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_BLOCK_VALIDATION_INTERNAL_H
