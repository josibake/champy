// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_chain_activation.h>

#include <chain.h>
#include <chainstate.h>
#include <chainstate_cache.h>
#include <chainstate_event_sink.h>
#include <coins.h>
#include <consensus/expected.h>
#include <primitives/block.h>
#include <tinyformat.h>
#include <uint256.h>
#include <util/check.h>
#include <util/log.h>
#include <util/translation.h>
#include <validation/block_connection.h>
#include <validation/block_connection_state.h>
#include <validation/block_index.h>
#include <validation/block_storage.h>
#include <validation/block_validation.h>
#include <validation/block_validation_error.h>
#include <validation/core_block_commit_adapters.h>
#include <validation/core_block_connection_context.h>
#include <validation/core_block_connection_setup.h>
#include <validation/core_block_connection_snapshot.h>
#include <validation/core_chain_lock.h>
#include <validation/core_chain_validation_runtimes.h>
#include <validation/core_validation_event_snapshot.h>
#include <validation/script_task_executor.h>
#include <validation/validation_event_queue.h>
#include <validation_state.h>

#include <algorithm>
#include <cassert>
#include <exception>
#include <new>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

using kernel::Notifications;

namespace {

ExternalCacheUsage ExternalCacheUsageForEvents(const ChainstateEventSink* chain_events)
{
    return chain_events ? chain_events->CacheUsage() : ExternalCacheUsage{};
}

Consensus::BlockCommitError TaintedConnectionAttemptCommitError(
    Consensus::ValidationRuntimeIssue runtime_issue,
    std::string reject_reason)
{
    return {
        .runtime_issue = runtime_issue,
        .failure_state = Consensus::BlockCommitFailureState::Tainted,
        .reject_reason = std::move(reject_reason),
    };
}

Consensus::BlockCommitResult<void> CommitConnectionAttempt(validation::BlockConnectionAttemptGuard& connection_attempt)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    try {
        connection_attempt.Commit();
    } catch (const std::logic_error&) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedConnectionAttemptCommitError(
            Consensus::ValidationRuntimeIssue::CommitConflict,
            "block-connection-flush-conflict")};
    } catch (const std::bad_alloc&) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedConnectionAttemptCommitError(
            Consensus::ValidationRuntimeIssue::ResourceLimit,
            "block-connection-flush-resource-limit")};
    } catch (const std::exception&) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedConnectionAttemptCommitError(
            Consensus::ValidationRuntimeIssue::SystemError,
            "block-connection-flush-failed")};
    } catch (...) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedConnectionAttemptCommitError(
            Consensus::ValidationRuntimeIssue::SystemError,
            "block-connection-flush-failed")};
    }

    return {};
}

void AbortAfterTaintedBlockCommit(CoreActivationRuntime& runtime, BlockValidationState& state)
{
    FatalError(
        runtime.Notifications(),
        state,
        _("Block commit failed after mutating chainstate; restart with a fresh chainstate view is required."));
}

std::shared_ptr<const CBlock> LoadBlockForConnection(
    Notifications& notifications,
    BlockValidationState& state,
    CBlockIndex& block_index,
    std::shared_ptr<const CBlock> cached_block,
    BlockDataReader& block_reader,
    CoreChainLock* chain_lock) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    if (cached_block) {
        LogDebug(BCLog::BENCH, "  - Using cached block\n");
        return cached_block;
    }

    const FlatFilePos block_pos{block_index.GetBlockPos()};
    const uint256 expected_hash{block_index.GetBlockHash()};
    const auto read_block{[&]() {
        return block_reader.ReadBlockFromPosition(block_pos, expected_hash);
    }};
    auto read_result{chain_lock ? chain_lock->RunUnlocked(read_block) : read_block()};
    if (!read_result) {
        FatalError(notifications, state, _("Failed to read block."));
        return nullptr;
    }
    return std::make_shared<CBlock>(std::move(*read_result));
}

CoreBlockConnectionRuntimeInputs MakeCoreBlockConnectionRuntimeInputs(
    CoreActivationRuntime& runtime,
    validation::ScriptTaskExecutor& script_task_executor,
    CoreChainLock* chain_lock)
{
    return {
        .notifications = runtime.Notifications(),
        .script_task_executor = script_task_executor,
        .validation_cache = runtime.ScriptValidationCache(),
        .chain_lock = chain_lock,
    };
}

std::optional<validation::BlockConnectionCommitPackage> RunPreparedBlockConnection(
    BlockValidationState& state,
    const uint256& block_hash,
    const validation::BlockConnectionRequest& request)
{
    validation::BlockConnectionResult connection_result{validation::BlockConnectionEngine{}.ConnectPrepared(request, state)};
    if (!connection_result.Succeeded()) {
        LogError("%s: Block connection %s failed, %s\n", "ConnectTip", block_hash.ToString(), FormatValidationStateForLog(state));
        return std::nullopt;
    }
    assert(connection_result.commit_package);
    return std::move(*connection_result.commit_package);
}

void AccumulateAndLogConnectTipStep(
    const char* label,
    SteadyClock::duration elapsed,
    SteadyClock::duration& total,
    int64_t blocks_total) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    total += elapsed;
    assert(blocks_total > 0);
    LogDebug(BCLog::BENCH, "  - %s: %.2fms [%.2fs (%.2fms/blk)]\n",
             label,
             Ticks<MillisecondsDouble>(elapsed),
             Ticks<SecondsDouble>(total),
             Ticks<MillisecondsDouble>(total) / blocks_total);
}

void AccumulateAndLogConnectTipTotal(
    SteadyClock::duration elapsed,
    SteadyClock::duration& total,
    int64_t blocks_total) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    total += elapsed;
    assert(blocks_total > 0);
    LogDebug(BCLog::BENCH, "- Connect block: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(elapsed),
             Ticks<SecondsDouble>(total),
             Ticks<MillisecondsDouble>(total) / blocks_total);
}

void PublishConnectedBlock(
    ChainstateEventSink* chain_events,
    const CBlock& block,
    int height) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    if (chain_events) {
        chain_events->BlockConnected(block, height);
    }
}

struct CoreChainFork {
    const CBlockIndex* old_tip{nullptr};
    const CBlockIndex* fork{nullptr};
};

class ScopedChainLockReleaseOverride
{
public:
    ScopedChainLockReleaseOverride(CoreChainActivationResources& resources, CoreChainLock* replacement)
        : m_resources{resources},
          m_original{std::exchange(resources.chain_lock, replacement)}
    {
    }

    ~ScopedChainLockReleaseOverride()
    {
        m_resources.chain_lock = m_original;
    }

    ScopedChainLockReleaseOverride(const ScopedChainLockReleaseOverride&) = delete;
    ScopedChainLockReleaseOverride& operator=(const ScopedChainLockReleaseOverride&) = delete;

private:
    CoreChainActivationResources& m_resources;
    CoreChainLock* m_original;
};

class ScopedChainEventSinkOverride
{
public:
    ScopedChainEventSinkOverride(CoreChainActivationResources& resources, ChainstateEventSink* replacement)
        : m_resources{resources},
          m_original{std::exchange(resources.chain_events, replacement)}
    {
    }

    ~ScopedChainEventSinkOverride()
    {
        m_resources.chain_events = m_original;
    }

    ScopedChainEventSinkOverride(const ScopedChainEventSinkOverride&) = delete;
    ScopedChainEventSinkOverride& operator=(const ScopedChainEventSinkOverride&) = delete;

    [[nodiscard]] ChainstateEventSink* Original() const noexcept { return m_original; }

private:
    CoreChainActivationResources& m_resources;
    ChainstateEventSink* m_original;
};

CoreChainFork LocateCoreChainFork(CoreChainActivationState& active_chain, CBlockIndex& index_most_work)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    return {
        .old_tip = active_chain.Tip(),
        .fork = active_chain.FindFork(index_most_work),
    };
}

ChainWorkBlockSnapshot SnapshotCoreConnectTipPosition(const CBlockIndex& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    return {
        .hash = block_index.GetBlockHash(),
        .parent_hash = block_index.pprev ? block_index.pprev->GetBlockHash() : uint256{},
        .height = block_index.nHeight,
        .chain_work = block_index.nChainWork,
    };
}

CoreConnectTipPrepareResources PrepareResourcesFor(CoreChainActivationResources& resources)
{
    return {
        .runtime = resources.runtime,
        .block_reader = resources.block_reader,
        .block_index_lookup = resources.block_index_lookup,
        .connection_snapshotter = resources.connection_snapshotter,
        .last_script_check_reason_logged = resources.last_script_check_reason_logged,
        .chain_lock = resources.chain_lock,
    };
}

CoreConnectTipExecutionResources ExecutionResourcesFor(CoreChainActivationResources& resources)
{
    return {
        .runtime = resources.runtime,
        .chain_lock = resources.chain_lock,
    };
}

CoreConnectTipReportResources ReportResourcesFor(CoreChainActivationResources& resources)
{
    return {
        .runtime = resources.runtime,
        .block_index_lookup = resources.block_index_lookup,
        .validation_events = resources.validation_events,
        .time_connect_total = resources.timing.time_connect_total,
        .blocks_total = resources.timing.blocks_total,
    };
}

CoreConnectTipCommitResources CommitResourcesFor(CoreChainActivationResources& resources)
{
    return {
        .runtime = resources.runtime,
        .undo_writer = resources.undo_writer,
        .block_index_lookup = resources.block_index_lookup,
        .block_index_committer = resources.block_index_committer,
        .connection_state = resources.connection_state,
        .spend_state_committer = resources.spend_state_committer,
        .connected_blocks = resources.connected_blocks,
        .chain_events = resources.chain_events,
        .validation_events = &resources.validation_events,
        .current_time = resources.current_time,
        .report_block_checked = false,
        .timing = resources.timing,
        .activation_timings = resources.activation_timings,
        .activation_connected_blocks = resources.activation_connected_blocks,
    };
}

bool ReadyCommitMatchesCommitTarget(
    const CoreBlockConnectionCommitWork& work,
    const CBlock& block,
    const CBlockIndex& block_index,
    const CBlockIndex* active_tip) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    const uint256 block_parent{block_index.pprev ? block_index.pprev->GetBlockHash() : uint256{}};
    return work.block_position.hash == block.GetHash() &&
           work.block_position.hash == block_index.GetBlockHash() &&
           work.block_position.height == block_index.nHeight &&
           work.block_position.parent_hash == block_parent &&
           active_tip == block_index.pprev;
}

enum class DisconnectToForkStatus {
    Unchanged,
    Disconnected,
    Failed,
};

DisconnectToForkStatus DisconnectCoreChainToFork(
    CoreChainActivationState& active_chain,
    const CBlockIndex* fork,
    NodeSeconds current_time,
    ChainstateEventSink* chain_events,
    BlockValidationState& state) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    bool disconnected{false};
    while (active_chain.Tip() && active_chain.Tip() != fork) {
        if (!active_chain.DisconnectTip(state, current_time, chain_events)) {
            // This is likely a fatal error. Notify the event sink without
            // restoring disconnected transactions, just in case observers run
            // before shutdown.
            active_chain.NotifyReorgCompleted(chain_events, /*success=*/false);

            // If we're unable to disconnect a block during normal operation,
            // then that is a failure of our local system -- we should abort
            // rather than stay on a less work chain.
            FatalError(active_chain.Notifications(), state, _("Failed to disconnect block."));
            return DisconnectToForkStatus::Failed;
        }
        disconnected = true;
    }
    if (disconnected) active_chain.RefreshBlockIndexCandidates();

    return disconnected ? DisconnectToForkStatus::Disconnected : DisconnectToForkStatus::Unchanged;
}

std::vector<CBlockIndex*> NextCoreChainConnectBatch(CBlockIndex& index_most_work, int& height)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    const int target_height{std::min(height + 32, index_most_work.nHeight)};
    std::vector<CBlockIndex*> blocks_to_connect;
    blocks_to_connect.reserve(target_height - height);

    CBlockIndex* block_index{index_most_work.GetAncestor(target_height)};
    while (block_index && block_index->nHeight != height) {
        blocks_to_connect.push_back(block_index);
        block_index = block_index->pprev;
    }
    height = target_height;

    return blocks_to_connect;
}

std::optional<PreparedCoreConnectTip> PrepareCoreConnectTipInternal(CoreConnectTipRequest request, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    CoreConnectTipPrepareResources& resources{request.resources};
    assert(request.block_index.pprev == resources.runtime.ActiveTip());

    const auto time_start{SteadyClock::now()};
    std::shared_ptr<const CBlock> block_to_connect{LoadBlockForConnection(
        resources.runtime.Notifications(),
        state,
        request.block_index,
        std::move(request.cached_block),
        resources.block_reader,
        resources.chain_lock)};
    if (!block_to_connect) return std::nullopt;

    const auto time_block_loaded{SteadyClock::now()};
    LogDebug(BCLog::BENCH, "  - Load block from disk: %.2fms\n",
             Ticks<MillisecondsDouble>(time_block_loaded - time_start));

    CoreBlockConnectionPlan connection_plan{PlanCoreBlockConnection(
        resources.runtime.SnapshotConnectionPolicy(request.block_index),
        resources.block_index_lookup,
        request.block_index)};
    MaybeLogCoreBlockConnectionScriptPolicy(
        resources.last_script_check_reason_logged,
        request.block_index,
        block_to_connect->GetHash(),
        connection_plan);
    validation::SnapshotBlockConnectionState snapshot_state{resources.connection_snapshotter.Snapshot(*block_to_connect, request.block_index)};

    return PreparedCoreConnectTip{
        .block_position = SnapshotCoreConnectTipPosition(request.block_index),
        .trace_counters = resources.runtime.TraceCounters(),
        .block = std::move(block_to_connect),
        .connection_plan = std::move(connection_plan),
        .snapshot_state = std::move(snapshot_state),
        .time_start = time_start,
        .time_block_loaded = time_block_loaded,
    };
}

CoreConnectTipExecutionResult ExecutePreparedCoreConnectTipInternal(CoreConnectTipExecutionResources resources, PreparedCoreConnectTip prepared, BlockValidationState& state)
{
    std::shared_ptr<const CBlock> block{std::move(prepared.block)};
    BlockConnectionTrace trace{prepared.trace_counters};
    CoreBlockConnectionSetup connection_setup{
        MakeCoreBlockConnectionRuntimeInputs(
            resources.runtime,
            resources.runtime.ScriptTaskExecutor(),
            /*chain_lock=*/nullptr),
        std::move(prepared.connection_plan),
        trace,
        /*cache_script_results=*/false};
    const validation::BlockConnectionRequest request{connection_setup.Request(
        *block,
        prepared.snapshot_state)};

    const auto run_connection = [&]() {
        return RunPreparedBlockConnection(state, prepared.block_position.hash, request);
    };
    auto commit_package{resources.chain_lock ? resources.chain_lock->RunUnlocked(run_connection) : run_connection()};
    if (!commit_package) {
        return {
            .status = CoreConnectTipStatus::BlockConnectionFailed,
            .checked_block = std::move(block),
            .execution = std::nullopt,
        };
    }

    const auto time_block_connected{SteadyClock::now()};

    return {
        .status = CoreConnectTipStatus::Connected,
        .checked_block = block,
        .execution = ExecutedCoreConnectTip{
            .block_position = prepared.block_position,
            .block = std::move(block),
            .commit_package = std::move(*commit_package),
            .trace = std::move(trace),
            .time_start = prepared.time_start,
            .time_block_loaded = prepared.time_block_loaded,
            .time_block_connected = time_block_connected,
            .global_block_counted = prepared.trace_counters.num_blocks_total != nullptr,
        },
    };
}

void ReportCoreConnectTipExecutionInternal(
    CoreConnectTipReportResources resources,
    const CoreConnectTipExecutionResult& result,
    BlockValidationState& state) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    const std::shared_ptr<const CBlock>& block{Assert(result.checked_block)};

    resources.validation_events.BlockChecked({.block = block, .state = state});
    if (!result.execution) {
        if (state.IsInvalid()) {
            if (CBlockIndex * block_index{resources.block_index_lookup.LookupBlockIndex(block->GetHash())}) {
                resources.runtime.MarkInvalidBlockFound(*block_index, state);
            }
        }
        return;
    }

    const ExecutedCoreConnectTip& execution{*result.execution};
    AccumulateAndLogConnectTipStep(
        "Connect total",
        execution.time_block_connected - execution.time_block_loaded,
        resources.time_connect_total,
        resources.blocks_total);
}

CoreConnectTipResult CommitCoreBlockConnectionInternal(CoreConnectTipCommitResources resources, CoreBlockConnectionCommitWork work, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    const CBlock& block{*Assert(work.block)};
    CBlockIndex* index{resources.block_index_lookup.LookupBlockIndex(work.block_position.hash)};
    if (index == nullptr) {
        state.Error("stale block connection");
        return CoreConnectTipResult::BlockConnectionFailed();
    }
    CBlockIndex& block_index{*index};
    if (!ReadyCommitMatchesCommitTarget(work, block, block_index, resources.runtime.ActiveTip())) {
        state.Error("stale block connection");
        return CoreConnectTipResult::BlockConnectionFailed();
    }
    if (resources.report_block_checked) {
        Assert(resources.validation_events)->BlockChecked({.block = work.block, .state = state});
    }

    auto connection_attempt{resources.connection_state.BeginConnectionAttempt()};
    CoreBlockConnectionCommitTarget commit_target{
        resources.undo_writer,
        resources.block_index_committer,
        resources.connection_state,
        block_index};
    const validation::BlockConnectionCommitRequest commit_request{
        .runtime = {
            .revert_data_writer = commit_target.RevertDataWriter(),
            .spend_state_committer = resources.spend_state_committer,
            .metadata_committer = commit_target.MetadataCommitter(),
            .trace = work.trace,
        },
        .context = {
            .block = block,
            .block_position = commit_target.BlockPosition(),
            .connection_state = resources.connection_state,
        },
    };
    const validation::BlockConnectionResult commit_result{
        validation::BlockConnectionEngine{}.Commit(commit_request, std::move(work.commit_package), state)};
    if (!commit_result.Succeeded()) {
        if (commit_result.HasTaintedCommitFailure()) {
            AbortAfterTaintedBlockCommit(resources.runtime, state);
        }
        return CoreConnectTipResult::BlockConnectionFailed();
    }

    if (const auto attempt_commit{CommitConnectionAttempt(*connection_attempt)}; !attempt_commit) {
        ApplyBlockCommitError(state, attempt_commit.error());
        AbortAfterTaintedBlockCommit(resources.runtime, state);
        return CoreConnectTipResult::BlockConnectionFailed();
    }

    const BlockConnectionStageTimings connection_timings{work.trace.Timings()};
    resources.activation_timings.spend_join += connection_timings.spend_join;
    resources.activation_timings.script_validation += connection_timings.script_validation;
    ++resources.activation_connected_blocks;

    if (!work.global_block_counted) {
        ++resources.timing.blocks_total;
        work.global_block_counted = true;
    }

    const auto time_coins_committed{SteadyClock::now()};
    AccumulateAndLogConnectTipStep(
        "Flush",
        time_coins_committed - work.time_block_connected,
        resources.timing.time_flush,
        resources.timing.blocks_total);

    if (!resources.runtime.FlushActiveChainstateIfNeeded(state, ExternalCacheUsageForEvents(resources.chain_events))) {
        ApplyBlockCommitError(
            state,
            TaintedConnectionAttemptCommitError(
                Consensus::ValidationRuntimeIssue::SystemError,
                "chainstate-flush-failed-after-block-commit"));
        AbortAfterTaintedBlockCommit(resources.runtime, state);
        return CoreConnectTipResult::ChainstateFlushFailed();
    }

    const auto time_chainstate_persisted{SteadyClock::now()};
    AccumulateAndLogConnectTipStep(
        "Writing chainstate",
        time_chainstate_persisted - time_coins_committed,
        resources.timing.time_chainstate,
        resources.timing.blocks_total);

    PublishConnectedBlock(resources.chain_events, block, block_index.nHeight);
    resources.runtime.AdvanceActiveChainTip(block_index, resources.chain_events, resources.current_time);

    const auto time_tip_advanced{SteadyClock::now()};
    AccumulateAndLogConnectTipStep(
        "Connect postprocess",
        time_tip_advanced - time_chainstate_persisted,
        resources.timing.time_post_connect,
        resources.timing.blocks_total);
    AccumulateAndLogConnectTipTotal(
        time_tip_advanced - work.time_start,
        resources.timing.time_total,
        resources.timing.blocks_total);

    resources.connected_blocks.push_back({
        .block_info = validation::SnapshotCoreValidationBlockInfo(block_index),
        .pblock = std::move(work.block),
    });
    return CoreConnectTipResult::Connected();
}

CoreActivateBestChainStepResult ActivateCoreBestChainStepWithFork(CoreActivateBestChainStepRequest request, const CoreChainFork& activation_fork, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    CoreChainActivationState& active_chain{request.active_chain};

    const DisconnectToForkStatus disconnect_status{DisconnectCoreChainToFork(active_chain, activation_fork.fork, request.resources.current_time, request.resources.chain_events, state)};
    if (disconnect_status == DisconnectToForkStatus::Failed) {
        return CoreActivateBestChainStepResult::SystemError();
    }
    const bool blocks_disconnected{disconnect_status == DisconnectToForkStatus::Disconnected};

    bool continue_step{true};
    auto result{CoreActivateBestChainStepResult::Completed()};
    int height{activation_fork.fork ? activation_fork.fork->nHeight : -1};
    while (continue_step && height != request.index_most_work.nHeight) {
        // Don't iterate the entire list of potential improvements toward the
        // best tip, as we likely only need a few blocks along the way.
        const std::vector<CBlockIndex*> blocks_to_connect{NextCoreChainConnectBatch(request.index_most_work, height)};

        for (CBlockIndex* block_to_connect : blocks_to_connect | std::views::reverse) {
            auto prepared{PrepareCoreConnectTip(
                {
                    .resources = PrepareResourcesFor(request.resources),
                    .block_index = *block_to_connect,
                    .cached_block = block_to_connect == &request.index_most_work ? request.cached_best_block : std::shared_ptr<const CBlock>{},
                },
                state)};
            CoreConnectTipResult connect_result{CoreConnectTipResult::BlockReadFailed()};
            if (prepared) {
                auto execution{ExecutePreparedCoreConnectTip(ExecutionResourcesFor(request.resources), std::move(*prepared), state)};
                ReportCoreConnectTipExecution(ReportResourcesFor(request.resources), execution, state);
                if (execution.execution) {
                    connect_result = CommitCoreConnectTip(CommitResourcesFor(request.resources), std::move(*execution.execution), state);
                } else {
                    connect_result = CoreConnectTipResult{execution.status};
                }
            }
            if (!connect_result.Succeeded()) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (state.GetResult() != BlockValidationResult::BLOCK_MUTATED) {
                        active_chain.MarkInvalidChainFound(*blocks_to_connect.front());
                    }
                    state = BlockValidationState();
                    result = CoreActivateBestChainStepResult::InvalidChainFound();
                    continue_step = false;
                    break;
                }

                // A system error occurred (disk space, database error, ...).
                // Notify the event sink so observers see state consistent with
                // the current tip before shutdown.
                active_chain.NotifyReorgCompleted(request.resources.chain_events, /*success=*/false);
                return CoreActivateBestChainStepResult::SystemError();
            }

            active_chain.PruneBlockIndexCandidates();
            if (!activation_fork.old_tip || active_chain.Tip()->nChainWork > activation_fork.old_tip->nChainWork) {
                // We're in a better position than we were. Return temporarily
                // to release the lock.
                continue_step = false;
                break;
            }
        }
    }

    if (blocks_disconnected) {
        active_chain.NotifyReorgCompleted(request.resources.chain_events, /*success=*/true);
    }
    active_chain.CheckPostReorgState(request.resources.chain_events);
    active_chain.CheckForkWarningConditions();

    return result;
}

} // namespace

CoreBlockConnectionCommitWork MakeCoreBlockConnectionCommitWork(
    ChainWorkBlockSnapshot block_position,
    std::shared_ptr<const CBlock> block,
    validation::BlockConnectionCommitPackage commit_package,
    BlockConnectionTraceCounters trace_counters)
{
    assert(block);
    const auto now{SteadyClock::now()};
    BlockConnectionTrace trace{trace_counters};
    trace.CountBlock();
    trace.SanityChecksDone();
    trace.ForkChecksDone();
    if (commit_package.effects) {
        trace.SpendStageValidated(block->vtx.size(), commit_package.effects->inputs);
        trace.SpendStageCompleted(commit_package.effects->inputs);
    }

    return {
        .block_position = std::move(block_position),
        .block = std::move(block),
        .commit_package = std::move(commit_package),
        .trace = std::move(trace),
        .time_start = now,
        .time_block_loaded = now,
        .time_block_connected = now,
        .global_block_counted = trace_counters.num_blocks_total != nullptr,
    };
}

std::optional<PreparedCoreConnectTip> PrepareCoreConnectTip(CoreConnectTipRequest request, BlockValidationState& state)
{
    return PrepareCoreConnectTipInternal(std::move(request), state);
}

CoreConnectTipExecutionResult ExecutePreparedCoreConnectTip(CoreConnectTipExecutionResources resources, PreparedCoreConnectTip prepared, BlockValidationState& state)
{
    return ExecutePreparedCoreConnectTipInternal(resources, std::move(prepared), state);
}

void ReportCoreConnectTipExecution(
    CoreConnectTipReportResources resources,
    const CoreConnectTipExecutionResult& result,
    BlockValidationState& state)
{
    ReportCoreConnectTipExecutionInternal(resources, result, state);
}

CoreConnectTipResult CommitCoreBlockConnection(CoreConnectTipCommitResources resources, CoreBlockConnectionCommitWork work, BlockValidationState& state)
{
    return CommitCoreBlockConnectionInternal(resources, std::move(work), state);
}

CoreConnectTipResult CommitCoreConnectTip(CoreConnectTipCommitResources resources, ExecutedCoreConnectTip execution, BlockValidationState& state)
{
    return CommitCoreBlockConnection(std::move(resources), std::move(execution), state);
}

const CBlockIndex* CoreChainActivationState::Tip() const
{
    return m_chainstate.m_chain.Tip();
}

const CBlockIndex* CoreChainActivationState::FindFork(const CBlockIndex& block_index) const
{
    return m_chainstate.m_chain.FindFork(block_index);
}

Notifications& CoreChainActivationState::Notifications() const
{
    return m_chainstate.m_chainman.GetNotifications();
}

bool CoreChainActivationState::DisconnectTip(BlockValidationState& state, NodeSeconds current_time, ChainstateEventSink* chain_events) const
{
    return m_chainstate.DisconnectTip(state, current_time, chain_events);
}

void CoreChainActivationState::PruneBlockIndexCandidates() const
{
    m_chainstate.PruneBlockIndexCandidates();
}

void CoreChainActivationState::RefreshBlockIndexCandidates() const
{
    m_chainstate.RefreshBlockIndexCandidates();
}

void CoreChainActivationState::MarkInvalidChainFound(CBlockIndex& block_index) const
{
    m_chainstate.InvalidChainFound(&block_index);
}

void CoreChainActivationState::NotifyReorgCompleted(ChainstateEventSink* chain_events, bool success) const
{
    if (chain_events) chain_events->ReorgCompleted(success);
}

void CoreChainActivationState::CheckPostReorgState(ChainstateEventSink* chain_events) const
{
    if (!chain_events) return;

    chain_events->CheckPostReorgState(m_chainstate.m_chain.Height() + 1);
}

void CoreChainActivationState::CheckForkWarningConditions() const
{
    m_chainstate.CheckForkWarningConditions();
}

CoreActivateBestChainStepResult ActivateCoreBestChainStep(CoreActivateBestChainStepRequest request, BlockValidationState& state)
{
    AssertLockHeld(cs_main);
    CoreChainActivationState& active_chain{request.active_chain};
    const CoreChainFork activation_fork{LocateCoreChainFork(active_chain, request.index_most_work)};
    const bool reorg_repair_lock_needed{request.resources.chain_events && activation_fork.old_tip != activation_fork.fork};

    if (reorg_repair_lock_needed) {
        ChainstateEventRecorder buffered_events{request.resources.chain_events->CacheUsage()};
        // Reorg repair must not be visible until the chain transition reaches a
        // stable point. Keep cs_main held and buffer node events, then let the
        // node sink apply the batch before cs_main is released.
        const ScopedChainLockReleaseOverride keep_chain_lock_held{request.resources, nullptr};
        const ScopedChainEventSinkOverride buffer_chain_events{request.resources, &buffered_events};
        const CoreActivateBestChainStepResult result{ActivateCoreBestChainStepWithFork(std::move(request), activation_fork, state)};
        buffer_chain_events.Original()->ProcessEvents(buffered_events.Events());
        return result;
    }

    return ActivateCoreBestChainStepWithFork(std::move(request), activation_fork, state);
}
