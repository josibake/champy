// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_CHAIN_ACTIVATION_H
#define BITCOIN_VALIDATION_CORE_CHAIN_ACTIVATION_H

#include <kernel/cs_main.h>
#include <util/time.h>
#include <validation/block_connection.h>
#include <validation/block_connection_state.h>
#include <validation/block_connection_trace.h>
#include <validation/block_index_snapshot.h>
#include <validation/core_block_connection_context.h>
#include <validation/snapshot_block_connection_state.h>
#include <validation/validation_event_queue.h>

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
class ChainstateEventSink;
class CoreActivationRuntime;
class CoreChainLock;
struct BlockActivationTimings;

namespace kernel {
class Notifications;
} // namespace kernel
namespace validation {
class BlockConnectionState;
class CoreCoinsBlockConnectionSnapshotter;
} // namespace validation

struct ConnectedBlock {
    validation::ValidationBlockInfo block_info;
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

    bool DisconnectTip(BlockValidationState& state, NodeSeconds current_time, ChainstateEventSink* chain_events) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void PruneBlockIndexCandidates() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void RefreshBlockIndexCandidates() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
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

struct CoreChainActivationResources {
    CoreActivationRuntime& runtime;
    BlockDataReader& block_reader;
    BlockUndoWriter& undo_writer;
    BlockIndexLookup& block_index_lookup;
    BlockIndexValidityCommitter& block_index_committer;
    validation::BlockConnectionState& connection_state;
    validation::CoreCoinsBlockConnectionSnapshotter& connection_snapshotter;
    Consensus::SpendCommitter& spend_state_committer;
    std::optional<const char*>& last_script_check_reason_logged;
    std::vector<ConnectedBlock>& connected_blocks;
    ChainstateEventSink* chain_events{nullptr};
    validation::ValidationEventQueue& validation_events;
    NodeSeconds current_time;
    CoreConnectTipTiming timing;
    BlockActivationTimings& activation_timings;
    uint64_t& activation_connected_blocks;
    CoreChainLock* chain_lock{nullptr};
};

struct CoreConnectTipPrepareResources {
    CoreActivationRuntime& runtime;
    BlockDataReader& block_reader;
    BlockIndexLookup& block_index_lookup;
    validation::CoreCoinsBlockConnectionSnapshotter& connection_snapshotter;
    std::optional<const char*>& last_script_check_reason_logged;
    CoreChainLock* chain_lock{nullptr};
};

struct CoreConnectTipExecutionResources {
    CoreActivationRuntime& runtime;
    CoreChainLock* chain_lock{nullptr};
};

struct CoreConnectTipReportResources {
    CoreActivationRuntime& runtime;
    BlockIndexLookup& block_index_lookup;
    validation::ValidationEventQueue& validation_events;
    SteadyClock::duration& time_connect_total;
    int64_t& blocks_total;
};

struct CoreConnectTipCommitResources {
    CoreActivationRuntime& runtime;
    BlockUndoWriter& undo_writer;
    BlockIndexLookup& block_index_lookup;
    BlockIndexValidityCommitter& block_index_committer;
    validation::BlockConnectionState& connection_state;
    Consensus::SpendCommitter& spend_state_committer;
    std::vector<ConnectedBlock>& connected_blocks;
    ChainstateEventSink* chain_events{nullptr};
    validation::ValidationEventQueue* validation_events{nullptr};
    NodeSeconds current_time;
    bool report_block_checked{false};
    CoreConnectTipTiming timing;
    BlockActivationTimings& activation_timings;
    uint64_t& activation_connected_blocks;
};

struct CoreConnectTipRequest {
    CoreConnectTipPrepareResources resources;
    CBlockIndex& block_index;
    std::shared_ptr<const CBlock> cached_block;
};

/**
 * Staged Core connection values for one active-chain tip.
 *
 * These values keep block loading, spend/script execution, and commit separate.
 * Runtime resources are supplied explicitly to execution and commit.
 */
struct PreparedCoreConnectTip {
    ChainWorkBlockSnapshot block_position;
    BlockConnectionTraceCounters trace_counters;
    std::shared_ptr<const CBlock> block;
    CoreBlockConnectionPlan connection_plan;
    validation::SnapshotBlockConnectionState snapshot_state;
    SteadyClock::time_point time_start;
    SteadyClock::time_point time_block_loaded;
};

struct CoreBlockConnectionCommitWork {
    ChainWorkBlockSnapshot block_position;
    std::shared_ptr<const CBlock> block;
    validation::BlockConnectionCommitPackage commit_package;
    BlockConnectionTrace trace;
    SteadyClock::time_point time_start;
    SteadyClock::time_point time_block_loaded;
    SteadyClock::time_point time_block_connected;
    bool global_block_counted{false};
};

using ExecutedCoreConnectTip = CoreBlockConnectionCommitWork;

[[nodiscard]] CoreBlockConnectionCommitWork MakeCoreBlockConnectionCommitWork(
    ChainWorkBlockSnapshot block_position,
    std::shared_ptr<const CBlock> block,
    validation::BlockConnectionCommitPackage commit_package,
    BlockConnectionTraceCounters trace_counters = {});

struct CoreConnectTipExecutionResult {
    CoreConnectTipStatus status{CoreConnectTipStatus::BlockConnectionFailed};
    std::shared_ptr<const CBlock> checked_block;
    std::optional<ExecutedCoreConnectTip> execution;
};

[[nodiscard]] std::optional<PreparedCoreConnectTip> PrepareCoreConnectTip(CoreConnectTipRequest request, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

[[nodiscard]] CoreConnectTipExecutionResult ExecutePreparedCoreConnectTip(CoreConnectTipExecutionResources resources, PreparedCoreConnectTip prepared, BlockValidationState& state);

void ReportCoreConnectTipExecution(
    CoreConnectTipReportResources resources,
    const CoreConnectTipExecutionResult& result,
    BlockValidationState& state) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

[[nodiscard]] CoreConnectTipResult CommitCoreBlockConnection(CoreConnectTipCommitResources resources, CoreBlockConnectionCommitWork work, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

[[nodiscard]] CoreConnectTipResult CommitCoreConnectTip(CoreConnectTipCommitResources resources, ExecutedCoreConnectTip execution, BlockValidationState& state)
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
    CoreChainActivationResources& resources;
    CBlockIndex& index_most_work;
    std::shared_ptr<const CBlock> cached_best_block;
};

[[nodiscard]] CoreActivateBestChainStepResult ActivateCoreBestChainStep(CoreActivateBestChainStepRequest request, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_VALIDATION_CORE_CHAIN_ACTIVATION_H
