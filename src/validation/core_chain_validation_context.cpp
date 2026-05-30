// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_chain_validation_context.h>

#include <chainstate.h>
#include <consensus/params.h>
#include <validation/block_data_adapters.h>
#include <validation/block_header_context_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_connection_trace.h>
#include <validation_state.h>

const Consensus::Params& CoreChainValidationContext::ConsensusParams() const
{
    return m_chainman.GetConsensus();
}

const arith_uint256& CoreChainValidationContext::MinimumChainWork() const
{
    return m_chainman.MinimumChainWork();
}

const uint256& CoreChainValidationContext::AssumedValidBlock() const
{
    return m_chainman.AssumedValidBlock();
}

const CBlockIndex* CoreChainValidationContext::BestHeader() const
{
    return m_chainman.m_best_header;
}

CBlockIndex* CoreChainValidationContext::ActiveTip() const
{
    return m_chainman.ActiveTip();
}

int CoreChainValidationContext::ActiveHeight() const
{
    return m_chainman.ActiveHeight();
}

bool CoreChainValidationContext::IsInitialBlockDownload() const
{
    return m_chainman.IsInitialBlockDownload();
}

ValidationSignals* CoreChainValidationContext::Signals() const
{
    return m_chainman.m_options.signals;
}

CoreBlockDataStore CoreChainValidationContext::MakeBlockDataStore() const
{
    return CoreBlockDataStore{m_chainman.m_blockman};
}

CoreBlockHeaderContextProvider CoreChainValidationContext::MakeHeaderContextProvider() const
{
    return CoreBlockHeaderContextProvider{m_chainman};
}

CoreBlockIndexStore CoreChainValidationContext::MakeBlockIndexStore() const
{
    return CoreBlockIndexStore{m_chainman};
}

kernel::Notifications& CoreChainValidationContext::Notifications() const
{
    return m_chainman.GetNotifications();
}

CCheckQueue<CScriptCheck>& CoreChainValidationContext::ScriptCheckQueue() const
{
    return m_chainman.GetCheckQueue();
}

ValidationCache& CoreChainValidationContext::ScriptValidationCache() const
{
    return m_chainman.m_validation_cache;
}

BlockConnectionTraceCounters CoreChainValidationContext::TraceCounters() const
{
    return BlockConnectionTraceCountersFor(m_chainman);
}

void CoreChainValidationContext::CheckBlockIndex() const
{
    m_chainman.CheckBlockIndex();
}

bool CoreChainValidationContext::NotifyHeaderTip() const
{
    return m_chainman.NotifyHeaderTip();
}

void CoreChainValidationContext::MarkInvalidBlockFound(CBlockIndex& block_index, BlockValidationState& state) const
{
    m_chainman.ActiveChainstate().InvalidBlockFound(&block_index, state);
}

void CoreChainValidationContext::AdvanceActiveChainTip(CBlockIndex& block_index, ChainstateEventSink* chain_events) const
{
    m_chainman.ActiveChainstate().AdvanceActiveChainTip(block_index, chain_events);
}

bool CoreChainValidationContext::FlushActiveChainstateToDisk(BlockValidationState& state, FlushStateMode mode) const
{
    return m_chainman.ActiveChainstate().FlushStateToDisk(state, mode);
}

bool CoreChainValidationContext::FlushActiveChainstateIfNeeded(BlockValidationState& state, ExternalCacheUsage external_cache_usage) const
{
    return m_chainman.ActiveChainstate().FlushStateToDisk(state, FlushStateMode::IF_NEEDED, /*nManualPruneHeight=*/0, external_cache_usage);
}

bool CoreChainValidationContext::ActivateBestChain(BlockValidationState& state, const std::shared_ptr<const CBlock>& block, ChainstateEventSink* chain_events) const
{
    return m_chainman.ActiveChainstate().ActivateBestChain(state, block, chain_events);
}
