// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_CHAIN_ACTIVATION_H
#define BITCOIN_VALIDATION_CORE_CHAIN_ACTIVATION_H

#include <kernel/cs_main.h>
#include <util/time.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class BlockDataReader;
class BlockIndexLookup;
class BlockIndexValidityCommitter;
class BlockUndoWriter;
class BlockValidationState;
class Chainstate;
class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class ChainstateEventSink;
class CoreChainValidationContext;
class ValidationSignals;

namespace kernel {
class Notifications;
} // namespace kernel

struct ConnectedBlock {
    const CBlockIndex* pindex;
    std::shared_ptr<const CBlock> pblock;
};

enum class CoreConnectTipStatus {
    Connected,
    BlockReadFailed,
    BlockConnectionFailed,
    ChainstateFlushFailed,
};

struct CoreConnectTipResult {
    CoreConnectTipStatus status{CoreConnectTipStatus::BlockConnectionFailed};

    [[nodiscard]] static CoreConnectTipResult Connected() noexcept { return {CoreConnectTipStatus::Connected}; }
    [[nodiscard]] static CoreConnectTipResult BlockReadFailed() noexcept { return {CoreConnectTipStatus::BlockReadFailed}; }
    [[nodiscard]] static CoreConnectTipResult BlockConnectionFailed() noexcept { return {CoreConnectTipStatus::BlockConnectionFailed}; }
    [[nodiscard]] static CoreConnectTipResult ChainstateFlushFailed() noexcept { return {CoreConnectTipStatus::ChainstateFlushFailed}; }
    [[nodiscard]] bool Succeeded() const noexcept { return status == CoreConnectTipStatus::Connected; }
};

class CoreChainActivationState final
{
public:
    explicit CoreChainActivationState(Chainstate& chainstate) : m_chainstate{chainstate} {}

    [[nodiscard]] const CBlockIndex* Tip() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] const CBlockIndex* FindFork(const CBlockIndex& block_index) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] kernel::Notifications& Notifications() const;

    bool DisconnectTip(BlockValidationState& state, ChainstateEventSink* chain_events) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void PruneBlockIndexCandidates() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void MarkInvalidChainFound(CBlockIndex& block_index) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void NotifyReorgCompleted(ChainstateEventSink* chain_events, bool success) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void CheckPostReorgState(ChainstateEventSink* chain_events) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void CheckForkWarningConditions() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    Chainstate& m_chainstate;
};

struct CoreConnectTipTiming {
    SteadyClock::duration& time_connect_total;
    SteadyClock::duration& time_flush;
    SteadyClock::duration& time_chainstate;
    SteadyClock::duration& time_post_connect;
    SteadyClock::duration& time_total;
    int64_t& blocks_total;
};

struct CoreConnectTipResources {
    CoreChainValidationContext& context;
    BlockDataReader& block_reader;
    BlockUndoWriter& undo_writer;
    BlockIndexLookup& block_index_lookup;
    BlockIndexValidityCommitter& block_index_committer;
    CCoinsViewCache& connection_view;
    std::optional<const char*>& last_script_check_reason_logged;
    std::vector<ConnectedBlock>& connected_blocks;
    ChainstateEventSink* chain_events{nullptr};
    ValidationSignals* signals{nullptr};
    CoreConnectTipTiming timing;
};

struct CoreConnectTipRequest {
    CoreConnectTipResources& resources;
    CBlockIndex& block_index;
    std::shared_ptr<const CBlock> cached_block;
};

[[nodiscard]] CoreConnectTipResult ConnectCoreChainTip(CoreConnectTipRequest request, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

enum class CoreActivateBestChainStepStatus {
    Completed,
    InvalidChainFound,
    SystemError,
};

struct CoreActivateBestChainStepResult {
    CoreActivateBestChainStepStatus status{CoreActivateBestChainStepStatus::SystemError};

    [[nodiscard]] static CoreActivateBestChainStepResult Completed() noexcept { return {CoreActivateBestChainStepStatus::Completed}; }
    [[nodiscard]] static CoreActivateBestChainStepResult InvalidChainFound() noexcept { return {CoreActivateBestChainStepStatus::InvalidChainFound}; }
    [[nodiscard]] static CoreActivateBestChainStepResult SystemError() noexcept { return {CoreActivateBestChainStepStatus::SystemError}; }

    [[nodiscard]] bool HasSystemError() const noexcept { return status == CoreActivateBestChainStepStatus::SystemError; }
    [[nodiscard]] bool FoundInvalidChain() const noexcept { return status == CoreActivateBestChainStepStatus::InvalidChainFound; }
};

struct CoreActivateBestChainStepRequest {
    CoreChainActivationState& active_chain;
    CoreConnectTipResources& connection;
    CBlockIndex& index_most_work;
    std::shared_ptr<const CBlock> cached_best_block;
};

[[nodiscard]] CoreActivateBestChainStepResult ActivateCoreBestChainStep(CoreActivateBestChainStepRequest request, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_VALIDATION_CORE_CHAIN_ACTIVATION_H
