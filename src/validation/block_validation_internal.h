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
class Chainstate;
class ChainstateEventSink;
class CoreChainValidationContext;

[[nodiscard]] NewBlockHeadersResult ProcessNewBlockHeaders(CoreChainValidationContext& context, std::span<const CBlockHeader> headers, BlockHeaderAcceptanceOptions options, BlockValidationTime time, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockStructuralCheckResult CheckNewBlockStructural(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& block, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& pblock, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptNewBlockData(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& block, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] std::optional<NewBlockCandidateContextSnapshot> SnapshotAcceptedBlockContext(CoreChainValidationContext& context, const uint256& block_hash) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] bool ActivateAcceptedBlock(CoreChainValidationContext& context, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);

BlockValidationState TestBlockValidity(
    Chainstate& chainstate,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_BLOCK_VALIDATION_INTERNAL_H
