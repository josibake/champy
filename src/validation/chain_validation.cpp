// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/chain_validation.h>

#include <chainstate.h>
#include <validation/block_validation_internal.h>
#include <validation/core_chain_validation_context.h>

NewBlockHeadersResult ChainValidationService::ProcessNewBlockHeaders(
    std::span<const CBlockHeader> headers,
    BlockHeaderAcceptanceOptions options,
    BlockValidationTime time,
    BlockValidationState& state)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::ProcessNewBlockHeaders(context, headers, options, time, state);
}

NewBlockStructuralCheckResult ChainValidationService::CheckNewBlockStructural(
    const std::shared_ptr<const CBlock>& block,
    BlockValidationState& state)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::CheckNewBlockStructural(context, block, state);
}

BlockAcceptanceResult ChainValidationService::AcceptBlock(
    const std::shared_ptr<const CBlock>& block,
    BlockValidationState& state,
    BlockAcceptanceOptions options,
    BlockValidationTime time)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::AcceptBlock(context, block, state, options, time);
}

BlockAcceptanceResult ChainValidationService::AcceptNewBlockData(
    const std::shared_ptr<const CBlock>& block,
    BlockValidationState& state,
    BlockAcceptanceOptions options,
    BlockValidationTime time)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::AcceptNewBlockData(context, block, state, options, time);
}

std::optional<NewBlockCandidateContextSnapshot> ChainValidationService::SnapshotAcceptedBlockContext(
    const uint256& block_hash)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::SnapshotAcceptedBlockContext(context, block_hash);
}

BlockActivationResult ChainValidationService::ActivateAcceptedBlock(
    ChainstateEventSink* chain_events,
    const std::shared_ptr<const CBlock>& block,
    BlockValidationState& state)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::ActivateAcceptedBlock(context, chain_events, block, state);
}

void ChainValidationService::ReportBlockChecked(
    const std::shared_ptr<const CBlock>& block,
    const BlockValidationState& state)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    ::ReportBlockChecked(context, block, state);
}

NewBlockProcessingResult ChainValidationService::ProcessNewBlock(
    ChainstateEventSink* chain_events,
    const std::shared_ptr<const CBlock>& block,
    NewBlockProcessingOptions options,
    BlockValidationTime time)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::ProcessNewBlock(context, chain_events, block, options, time);
}

NewBlockProcessingResult ChainValidationService::ProcessNewBlock(
    const std::shared_ptr<const CBlock>& block,
    NewBlockProcessingOptions options,
    BlockValidationTime time)
{
    CoreChainValidationRuntime runtime{m_chainman};
    CoreChainValidationContext context{m_chainman, runtime};
    return ::ProcessNewBlock(context, block, options, time);
}

BlockValidationState ChainValidationService::TestActiveBlockValidity(
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time)
{
    LOCK(::cs_main);
    return TestActiveBlockValidityLocked(block, options, time);
}

BlockValidationState ChainValidationService::TestActiveBlockValidityLocked(
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time)
{
    return ::TestBlockValidity(m_chainman.ActiveChainstate(), block, options, time);
}
