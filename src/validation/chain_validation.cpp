// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/chain_validation.h>

#include <validation/block_validation_internal.h>
#include <validation/core_chain_validation_context.h>

NewBlockHeadersResult ChainValidationService::ProcessNewBlockHeaders(
    std::span<const CBlockHeader> headers,
    BlockHeaderAcceptanceOptions options,
    BlockValidationTime time,
    BlockValidationState& state)
{
    CoreChainValidationContext context{m_chainman};
    return ::ProcessNewBlockHeaders(context, headers, options, time, state);
}

BlockAcceptanceResult ChainValidationService::AcceptBlock(
    const std::shared_ptr<const CBlock>& block,
    BlockValidationState& state,
    BlockAcceptanceOptions options,
    BlockValidationTime time)
{
    CoreChainValidationContext context{m_chainman};
    return ::AcceptBlock(context, block, state, options, time);
}

NewBlockProcessingResult ChainValidationService::ProcessNewBlock(
    ChainstateEventSink* chain_events,
    const std::shared_ptr<const CBlock>& block,
    NewBlockProcessingOptions options,
    BlockValidationTime time)
{
    CoreChainValidationContext context{m_chainman};
    return ::ProcessNewBlock(context, chain_events, block, options, time);
}

NewBlockProcessingResult ChainValidationService::ProcessNewBlock(
    const std::shared_ptr<const CBlock>& block,
    NewBlockProcessingOptions options,
    BlockValidationTime time)
{
    CoreChainValidationContext context{m_chainman};
    return ::ProcessNewBlock(context, block, options, time);
}

BlockValidationState ChainValidationService::TestBlockValidity(
    Chainstate& chainstate,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time)
{
    return ::TestBlockValidity(chainstate, block, options, time);
}
