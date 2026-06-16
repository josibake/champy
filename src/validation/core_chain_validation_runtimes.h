// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_RUNTIMES_H
#define BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_RUNTIMES_H

#include <arith_uint256.h>
#include <chainstate_cache.h>
#include <kernel/cs_main.h>
#include <uint256.h>
#include <util/time.h>
#include <validation/block_connection_trace.h>
#include <validation/block_data_adapters.h>
#include <validation/block_header_context_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/core_block_connection_context.h>
#include <validation/core_check_queue_script_task_executor.h>
#include <validation/script_task_executor.h>
#include <validation/validation_event_queue.h>

#include <memory>
#include <optional>

class BlockValidationState;
struct BlockActivationResult;
class ChainstateManager;
class CBlock;
class CBlockIndex;
class ChainstateEventSink;
class ValidationCache;
enum class FlushStateMode : uint8_t;

namespace Consensus {
struct Params;
} // namespace Consensus

namespace kernel {
class Notifications;
} // namespace kernel

class CoreHeaderAdmissionRuntime final
{
public:
    explicit CoreHeaderAdmissionRuntime(ChainstateManager& chainman) : m_chainman{chainman} {}

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] CoreBlockHeaderContextProvider MakeHeaderContextProvider() const;
    [[nodiscard]] CoreBlockIndexStore MakeBlockIndexStore() const;
    void CheckBlockIndex() const;
    [[nodiscard]] bool NotifyHeaderTip() const LOCKS_EXCLUDED(::cs_main);
    [[nodiscard]] bool IsInitialBlockDownload() const;

private:
    ChainstateManager& m_chainman;
};

class CoreBlockDataAdmissionRuntime final
{
public:
    explicit CoreBlockDataAdmissionRuntime(ChainstateManager& chainman);

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] const arith_uint256& MinimumChainWork() const;
    [[nodiscard]] CBlockIndex* ActiveTip() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] int ActiveHeight() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] bool IsInitialBlockDownload() const;
    [[nodiscard]] validation::ValidationEventQueue& ValidationEvents() noexcept { return m_validation_events; }

    [[nodiscard]] CoreBlockDataStore MakeBlockDataStore() const;
    [[nodiscard]] CoreBlockHeaderContextProvider MakeHeaderContextProvider() const;
    [[nodiscard]] CoreBlockIndexStore MakeBlockIndexStore() const;
    [[nodiscard]] kernel::Notifications& Notifications() const;

    void CheckBlockIndex() const;
    void MarkInvalidBlockFound(CBlockIndex& block_index, BlockValidationState& state) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool FlushActiveChainstateToDisk(BlockValidationState& state, FlushStateMode mode) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    ChainstateManager& m_chainman;
    validation::CoreValidationEventQueue m_validation_events;
};

class CoreAcceptedContextReader final
{
public:
    explicit CoreAcceptedContextReader(ChainstateManager& chainman) : m_chainman{chainman} {}

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] CoreBlockHeaderContextProvider MakeHeaderContextProvider() const;
    [[nodiscard]] CoreBlockIndexStore MakeBlockIndexStore() const;

private:
    ChainstateManager& m_chainman;
};

class CoreActivationRuntime final
{
public:
    explicit CoreActivationRuntime(ChainstateManager& chainman);

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] const arith_uint256& MinimumChainWork() const;
    [[nodiscard]] const uint256& AssumedValidBlock() const;
    [[nodiscard]] const CBlockIndex* BestHeader() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] CBlockIndex* ActiveTip() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] validation::ValidationEventQueue& ValidationEvents() noexcept { return m_validation_events; }
    [[nodiscard]] validation::ScriptTaskExecutor& ScriptTaskExecutor();

    [[nodiscard]] CoreBlockDataStore MakeBlockDataStore() const;
    [[nodiscard]] CoreBlockHeaderContextProvider MakeHeaderContextProvider() const;
    [[nodiscard]] CoreBlockIndexStore MakeBlockIndexStore() const;
    [[nodiscard]] CoreBlockConnectionPolicySnapshot SnapshotConnectionPolicy(const CBlockIndex& block_index) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] kernel::Notifications& Notifications() const;
    [[nodiscard]] ValidationCache& ScriptValidationCache() const;
    [[nodiscard]] BlockConnectionTraceCounters TraceCounters() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    [[nodiscard]] bool NotifyHeaderTip() const LOCKS_EXCLUDED(::cs_main);
    void MarkInvalidBlockFound(CBlockIndex& block_index, BlockValidationState& state) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void AdvanceActiveChainTip(CBlockIndex& block_index, ChainstateEventSink* chain_events, NodeSeconds current_time) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool FlushActiveChainstateIfNeeded(BlockValidationState& state, ExternalCacheUsage external_cache_usage) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] BlockActivationResult ActivateBestChain(BlockValidationState& state, NodeSeconds current_time, const std::shared_ptr<const CBlock>& block, ChainstateEventSink* chain_events) const LOCKS_EXCLUDED(::cs_main);
    [[nodiscard]] BlockActivationResult ActivateMostWorkTipBlock(
        BlockValidationState& state,
        NodeSeconds current_time,
        CBlockIndex& block_index,
        const std::shared_ptr<const CBlock>& block,
        ChainstateEventSink* chain_events) const LOCKS_EXCLUDED(::cs_main);

private:
    ChainstateManager& m_chainman;
    std::optional<validation::CoreCheckQueueScriptTaskExecutor> m_script_task_executor;
    validation::CoreValidationEventQueue m_validation_events;
};

class CoreReplayRuntime final
{
public:
    explicit CoreReplayRuntime(ChainstateManager& chainman) : m_chainman{chainman} {}

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] CoreBlockConnectionPolicySnapshot SnapshotConnectionPolicy(const CBlockIndex& block_index) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] kernel::Notifications& Notifications() const;
    [[nodiscard]] ValidationCache& ScriptValidationCache() const;
    [[nodiscard]] BlockConnectionTraceCounters TraceCounters() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] validation::ScriptTaskExecutor& ScriptTaskExecutor();

private:
    ChainstateManager& m_chainman;
    std::optional<validation::CoreCheckQueueScriptTaskExecutor> m_script_task_executor;
};

class CoreTestBlockValidityRuntime final
{
public:
    explicit CoreTestBlockValidityRuntime(ChainstateManager& chainman) : m_chainman{chainman} {}

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] const arith_uint256& MinimumChainWork() const;
    [[nodiscard]] const uint256& AssumedValidBlock() const;
    [[nodiscard]] const CBlockIndex* BestHeader() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] CoreBlockHeaderContextProvider MakeHeaderContextProvider() const;
    [[nodiscard]] CoreBlockIndexStore MakeBlockIndexStore() const;
    [[nodiscard]] kernel::Notifications& Notifications() const;
    [[nodiscard]] ValidationCache& ScriptValidationCache() const;
    [[nodiscard]] BlockConnectionTraceCounters TraceCounters() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] validation::ScriptTaskExecutor& ScriptTaskExecutor();

private:
    ChainstateManager& m_chainman;
    std::optional<validation::CoreCheckQueueScriptTaskExecutor> m_script_task_executor;
};

#endif // BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_RUNTIMES_H
