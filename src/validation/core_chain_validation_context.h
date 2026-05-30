// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_CONTEXT_H
#define BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_CONTEXT_H

#include <arith_uint256.h>
#include <chainstate_cache.h>
#include <checkqueue.h>
#include <kernel/cs_main.h>
#include <script/script_check.h>
#include <uint256.h>

#include <cstdint>
#include <memory>

class BlockValidationState;
class ChainstateEventSink;
class ChainstateManager;
class CBlock;
class CBlockIndex;
class ValidationCache;
class ValidationSignals;
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

class CoreChainValidationContext final
{
public:
    explicit CoreChainValidationContext(ChainstateManager& chainman) : m_chainman{chainman} {}

    [[nodiscard]] const Consensus::Params& ConsensusParams() const;
    [[nodiscard]] const arith_uint256& MinimumChainWork() const;
    [[nodiscard]] const uint256& AssumedValidBlock() const;
    [[nodiscard]] const CBlockIndex* BestHeader() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] CBlockIndex* ActiveTip() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] int ActiveHeight() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] bool IsInitialBlockDownload() const;
    [[nodiscard]] ValidationSignals* Signals() const;

    [[nodiscard]] CoreBlockDataStore MakeBlockDataStore() const;
    [[nodiscard]] CoreBlockHeaderContextProvider MakeHeaderContextProvider() const;
    [[nodiscard]] CoreBlockIndexStore MakeBlockIndexStore() const;

    [[nodiscard]] kernel::Notifications& Notifications() const;
    [[nodiscard]] CCheckQueue<CScriptCheck>& ScriptCheckQueue() const;
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
};

#endif // BITCOIN_VALIDATION_CORE_CHAIN_VALIDATION_CONTEXT_H
