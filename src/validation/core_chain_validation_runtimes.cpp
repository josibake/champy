// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_chain_validation_runtimes.h>

#include <chainstate.h>
#include <consensus/params.h>
#include <validation_state.h>

namespace {

CoreBlockConnectionPolicySnapshot SnapshotCoreConnectionPolicy(ChainstateManager& chainman, const CBlockIndex& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    const CoreBlockHeaderContextProvider header_context{chainman};
    return SnapshotCoreBlockConnectionPolicy(
        chainman.GetConsensus(),
        header_context.BuildContext(block_index.pprev),
        {
            .assumed_valid_block = chainman.AssumedValidBlock(),
            .best_header = chainman.m_best_header,
            .minimum_chain_work = chainman.MinimumChainWork(),
        });
}

validation::ScriptTaskExecutor& ScriptTaskExecutorFor(
    ChainstateManager& chainman,
    std::optional<validation::CoreCheckQueueScriptTaskExecutor>& executor)
{
    if (!executor) {
        executor.emplace(chainman.GetCheckQueue());
    }
    return *executor;
}

} // namespace

const Consensus::Params& CoreHeaderAdmissionRuntime::ConsensusParams() const
{
    return m_chainman.GetConsensus();
}

CoreBlockHeaderContextProvider CoreHeaderAdmissionRuntime::MakeHeaderContextProvider() const
{
    return CoreBlockHeaderContextProvider{m_chainman};
}

CoreBlockIndexStore CoreHeaderAdmissionRuntime::MakeBlockIndexStore() const
{
    return CoreBlockIndexStore{m_chainman};
}

void CoreHeaderAdmissionRuntime::CheckBlockIndex() const
{
    m_chainman.CheckBlockIndex();
}

bool CoreHeaderAdmissionRuntime::NotifyHeaderTip() const
{
    return m_chainman.NotifyHeaderTip();
}

bool CoreHeaderAdmissionRuntime::IsInitialBlockDownload() const
{
    return m_chainman.IsInitialBlockDownload();
}

CoreBlockDataAdmissionRuntime::CoreBlockDataAdmissionRuntime(ChainstateManager& chainman)
    : m_chainman{chainman},
      m_validation_events{chainman.m_options.signals}
{
}

const Consensus::Params& CoreBlockDataAdmissionRuntime::ConsensusParams() const
{
    return m_chainman.GetConsensus();
}

const arith_uint256& CoreBlockDataAdmissionRuntime::MinimumChainWork() const
{
    return m_chainman.MinimumChainWork();
}

CBlockIndex* CoreBlockDataAdmissionRuntime::ActiveTip() const
{
    return m_chainman.ActiveTip();
}

int CoreBlockDataAdmissionRuntime::ActiveHeight() const
{
    return m_chainman.ActiveHeight();
}

bool CoreBlockDataAdmissionRuntime::IsInitialBlockDownload() const
{
    return m_chainman.IsInitialBlockDownload();
}

CoreBlockDataStore CoreBlockDataAdmissionRuntime::MakeBlockDataStore() const
{
    return CoreBlockDataStore{m_chainman.m_blockman};
}

CoreBlockHeaderContextProvider CoreBlockDataAdmissionRuntime::MakeHeaderContextProvider() const
{
    return CoreBlockHeaderContextProvider{m_chainman};
}

CoreBlockIndexStore CoreBlockDataAdmissionRuntime::MakeBlockIndexStore() const
{
    return CoreBlockIndexStore{m_chainman};
}

kernel::Notifications& CoreBlockDataAdmissionRuntime::Notifications() const
{
    return m_chainman.GetNotifications();
}

void CoreBlockDataAdmissionRuntime::CheckBlockIndex() const
{
    m_chainman.CheckBlockIndex();
}

void CoreBlockDataAdmissionRuntime::MarkInvalidBlockFound(CBlockIndex& block_index, BlockValidationState& state) const
{
    m_chainman.ActiveChainstate().InvalidBlockFound(&block_index, state);
}

bool CoreBlockDataAdmissionRuntime::FlushActiveChainstateToDisk(BlockValidationState& state, FlushStateMode mode) const
{
    return m_chainman.FlushActiveChainstateToDisk(state, mode);
}

const Consensus::Params& CoreAcceptedContextReader::ConsensusParams() const
{
    return m_chainman.GetConsensus();
}

CoreBlockHeaderContextProvider CoreAcceptedContextReader::MakeHeaderContextProvider() const
{
    return CoreBlockHeaderContextProvider{m_chainman};
}

CoreBlockIndexStore CoreAcceptedContextReader::MakeBlockIndexStore() const
{
    return CoreBlockIndexStore{m_chainman};
}

CoreActivationRuntime::CoreActivationRuntime(ChainstateManager& chainman)
    : m_chainman{chainman},
      m_validation_events{chainman.m_options.signals}
{
}

const Consensus::Params& CoreActivationRuntime::ConsensusParams() const
{
    return m_chainman.GetConsensus();
}

const arith_uint256& CoreActivationRuntime::MinimumChainWork() const
{
    return m_chainman.MinimumChainWork();
}

const uint256& CoreActivationRuntime::AssumedValidBlock() const
{
    return m_chainman.AssumedValidBlock();
}

const CBlockIndex* CoreActivationRuntime::BestHeader() const
{
    return m_chainman.m_best_header;
}

CBlockIndex* CoreActivationRuntime::ActiveTip() const
{
    return m_chainman.ActiveTip();
}

validation::ScriptTaskExecutor& CoreActivationRuntime::ScriptTaskExecutor()
{
    return ScriptTaskExecutorFor(m_chainman, m_script_task_executor);
}

CoreBlockDataStore CoreActivationRuntime::MakeBlockDataStore() const
{
    return CoreBlockDataStore{m_chainman.m_blockman};
}

CoreBlockHeaderContextProvider CoreActivationRuntime::MakeHeaderContextProvider() const
{
    return CoreBlockHeaderContextProvider{m_chainman};
}

CoreBlockIndexStore CoreActivationRuntime::MakeBlockIndexStore() const
{
    return CoreBlockIndexStore{m_chainman};
}

CoreBlockConnectionPolicySnapshot CoreActivationRuntime::SnapshotConnectionPolicy(const CBlockIndex& block_index) const
{
    return SnapshotCoreConnectionPolicy(m_chainman, block_index);
}

kernel::Notifications& CoreActivationRuntime::Notifications() const
{
    return m_chainman.GetNotifications();
}

ValidationCache& CoreActivationRuntime::ScriptValidationCache() const
{
    return m_chainman.m_validation_cache;
}

BlockConnectionTraceCounters CoreActivationRuntime::TraceCounters() const
{
    return BlockConnectionTraceCountersFor(m_chainman);
}

bool CoreActivationRuntime::NotifyHeaderTip() const
{
    return m_chainman.NotifyHeaderTip();
}

void CoreActivationRuntime::MarkInvalidBlockFound(CBlockIndex& block_index, BlockValidationState& state) const
{
    m_chainman.ActiveChainstate().InvalidBlockFound(&block_index, state);
}

void CoreActivationRuntime::AdvanceActiveChainTip(CBlockIndex& block_index, ChainstateEventSink* chain_events, NodeSeconds current_time) const
{
    m_chainman.ActiveChainstate().AdvanceActiveChainTip(block_index, chain_events, current_time);
}

bool CoreActivationRuntime::FlushActiveChainstateIfNeeded(BlockValidationState& state, ExternalCacheUsage external_cache_usage) const
{
    return m_chainman.FlushActiveChainstateIfNeeded(state, external_cache_usage);
}

BlockActivationResult CoreActivationRuntime::ActivateBestChain(BlockValidationState& state, NodeSeconds current_time, const std::shared_ptr<const CBlock>& block, ChainstateEventSink* chain_events) const
{
    return m_chainman.ActiveChainstate().ActivateBestChain(state, current_time, block, chain_events);
}

BlockActivationResult CoreActivationRuntime::ActivateMostWorkTipBlock(
    BlockValidationState& state,
    NodeSeconds current_time,
    CBlockIndex& block_index,
    const std::shared_ptr<const CBlock>& block,
    ChainstateEventSink* chain_events) const
{
    return m_chainman.ActiveChainstate().ActivateMostWorkTipBlock(state, current_time, block_index, block, chain_events);
}

const Consensus::Params& CoreReplayRuntime::ConsensusParams() const
{
    return m_chainman.GetConsensus();
}

CoreBlockConnectionPolicySnapshot CoreReplayRuntime::SnapshotConnectionPolicy(const CBlockIndex& block_index) const
{
    return SnapshotCoreConnectionPolicy(m_chainman, block_index);
}

kernel::Notifications& CoreReplayRuntime::Notifications() const
{
    return m_chainman.GetNotifications();
}

ValidationCache& CoreReplayRuntime::ScriptValidationCache() const
{
    return m_chainman.m_validation_cache;
}

BlockConnectionTraceCounters CoreReplayRuntime::TraceCounters() const
{
    return BlockConnectionTraceCountersFor(m_chainman);
}

validation::ScriptTaskExecutor& CoreReplayRuntime::ScriptTaskExecutor()
{
    return ScriptTaskExecutorFor(m_chainman, m_script_task_executor);
}

const Consensus::Params& CoreTestBlockValidityRuntime::ConsensusParams() const
{
    return m_chainman.GetConsensus();
}

const arith_uint256& CoreTestBlockValidityRuntime::MinimumChainWork() const
{
    return m_chainman.MinimumChainWork();
}

const uint256& CoreTestBlockValidityRuntime::AssumedValidBlock() const
{
    return m_chainman.AssumedValidBlock();
}

const CBlockIndex* CoreTestBlockValidityRuntime::BestHeader() const
{
    return m_chainman.m_best_header;
}

CoreBlockHeaderContextProvider CoreTestBlockValidityRuntime::MakeHeaderContextProvider() const
{
    return CoreBlockHeaderContextProvider{m_chainman};
}

CoreBlockIndexStore CoreTestBlockValidityRuntime::MakeBlockIndexStore() const
{
    return CoreBlockIndexStore{m_chainman};
}

kernel::Notifications& CoreTestBlockValidityRuntime::Notifications() const
{
    return m_chainman.GetNotifications();
}

ValidationCache& CoreTestBlockValidityRuntime::ScriptValidationCache() const
{
    return m_chainman.m_validation_cache;
}

BlockConnectionTraceCounters CoreTestBlockValidityRuntime::TraceCounters() const
{
    return BlockConnectionTraceCountersFor(m_chainman);
}

validation::ScriptTaskExecutor& CoreTestBlockValidityRuntime::ScriptTaskExecutor()
{
    return ScriptTaskExecutorFor(m_chainman, m_script_task_executor);
}
