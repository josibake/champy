// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_CONTEXT_H
#define BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_CONTEXT_H

#include <arith_uint256.h>
#include <chainstate_cache.h>
#include <kernel/cs_main.h>
#include <uint256.h>
#include <validation/script_check_scheduler.h>
#include <validation/validation_event_queue.h>

#include <cstdint>
#include <memory>

class BlockValidationState;
class ChainstateEventSink;
class ChainstateManager;
class CBlock;
class CBlockIndex;
class ValidationCache;
enum class FlushStateMode : uint8_t;

namespace Consensus {
struct Params;
} // namespace Consensus

namespace kernel {
class BlockManager;
class Notifications;
} // namespace kernel

class CoreBlockDataStore;
class CoreBlockHeaderContextProvider;
class CoreBlockIndexStore;
struct BlockConnectionTraceCounters;

class CoreChainValidationRuntime final
{
public:
    explicit CoreChainValidationRuntime(ChainstateManager& chainman);

    [[nodiscard]] validation::ScriptCheckScheduler& ScriptCheckScheduler() noexcept { return m_script_check_scheduler; }
    [[nodiscard]] validation::ValidationEventQueue& ValidationEvents() noexcept { return m_validation_events; }

private:
    validation::CCheckQueueScriptCheckScheduler m_script_check_scheduler;
    validation::CoreValidationEventQueue m_validation_events;
};

class CoreChainValidationContext final
{
public:
    CoreChainValidationContext(ChainstateManager& chainman, CoreChainValidationRuntime& runtime)
        : m_chainman{chainman}, m_runtime{runtime}
    {
    }

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] const arith_uint256& MinimumChainWork() const;
    [[nodiscard]] const uint256& AssumedValidBlock() const;
    [[nodiscard]] const CBlockIndex* BestHeader() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] CBlockIndex* ActiveTip() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] int ActiveHeight() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] bool IsInitialBlockDownload() const;
    [[nodiscard]] validation::ValidationEventQueue& ValidationEvents() const;
    [[nodiscard]] validation::ScriptCheckScheduler& ScriptCheckScheduler() const;

    [[nodiscard]] CoreBlockDataStore MakeBlockDataStore() const;
    [[nodiscard]] CoreBlockHeaderContextProvider MakeHeaderContextProvider() const;
    [[nodiscard]] CoreBlockIndexStore MakeBlockIndexStore() const;

    [[nodiscard]] kernel::Notifications& Notifications() const;
    [[nodiscard]] ValidationCache& ScriptValidationCache() const;
    [[nodiscard]] BlockConnectionTraceCounters TraceCounters() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    void CheckBlockIndex() const;
    bool NotifyHeaderTip() const LOCKS_EXCLUDED(::cs_main);
    void MarkInvalidBlockFound(CBlockIndex& block_index, BlockValidationState& state) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void AdvanceActiveChainTip(CBlockIndex& block_index, ChainstateEventSink* chain_events) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool FlushActiveChainstateToDisk(BlockValidationState& state, FlushStateMode mode) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool FlushActiveChainstateIfNeeded(BlockValidationState& state, ExternalCacheUsage external_cache_usage) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool ActivateBestChain(BlockValidationState& state, const std::shared_ptr<const CBlock>& block, ChainstateEventSink* chain_events) const LOCKS_EXCLUDED(::cs_main);

private:
    ChainstateManager& m_chainman;
    CoreChainValidationRuntime& m_runtime;
};

#endif // BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_CONTEXT_H
