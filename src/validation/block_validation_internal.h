// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_INTERNAL_H
#define BITCOIN_BLOCK_VALIDATION_INTERNAL_H

#include <kernel/cs_main.h>
#include <validation/block_validation.h>

#include <memory>
#include <optional>
#include <span>

class BlockHeaderContextProvider;
class BlockIndexLookup;
class BlockIndexValidityCommitter;
class BlockUndoWriter;
class Chainstate;
class ChainstateEventSink;
class CCoinsViewCache;
class CoreChainValidationContext;
namespace validation {
class ActiveChainView;
class ScriptCheckScheduler;
} // namespace validation

[[nodiscard]] NewBlockHeadersResult ProcessNewBlockHeaders(CoreChainValidationContext& context, std::span<const CBlockHeader> headers, BlockHeaderAcceptanceOptions options, BlockValidationTime time, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& pblock, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);

struct TestBlockValidityRequest {
    validation::ActiveChainView& active_chain;
    const Consensus::Params& consensus_params;
    BlockHeaderContextProvider& header_context;
    CCoinsViewCache& coins_tip;
    BlockUndoWriter& undo_writer;
    BlockIndexLookup& block_index_lookup;
    BlockIndexValidityCommitter& block_index_committer;
    CoreChainValidationContext& validation_context;
    validation::ScriptCheckScheduler& script_check_scheduler;
    std::optional<const char*>& last_script_check_reason_logged;
};

/**
 * Verify a block, including transactions. The block must connect to the current
 * tip of the supplied active chain.
 *
 * Returns a valid or invalid state. This does not currently return an error
 * state unless something is wrong with the existing chain state.
 */
BlockValidationState TestBlockValidity(
    TestBlockValidityRequest request,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

BlockValidationState TestBlockValidity(
    Chainstate& chainstate,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_BLOCK_VALIDATION_INTERNAL_H
