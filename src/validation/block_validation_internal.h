// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_INTERNAL_H
#define BITCOIN_BLOCK_VALIDATION_INTERNAL_H

#include <validation/block_validation.h>

#include <memory>
#include <span>

class CoreChainValidationContext;

[[nodiscard]] NewBlockHeadersResult ProcessNewBlockHeaders(CoreChainValidationContext& context, std::span<const CBlockHeader> headers, BlockHeaderAcceptanceOptions options, BlockValidationTime time, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& pblock, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time) LOCKS_EXCLUDED(cs_main);

/**
 * Verify a block, including transactions. The block must connect to the current
 * tip of the supplied chainstate.
 *
 * Returns a valid or invalid state. This does not currently return an error
 * state unless something is wrong with the existing chainstate.
 */
BlockValidationState TestBlockValidity(
    Chainstate& chainstate,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_BLOCK_VALIDATION_INTERNAL_H
