// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_chain_activation.h>

#include <chain.h>
#include <chainstate.h>
#include <chainstate_cache.h>
#include <chainstate_event_sink.h>
#include <coins.h>
#include <primitives/block.h>
#include <tinyformat.h>
#include <uint256.h>
#include <validation/block_connection.h>
#include <validation/block_connection_state.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_validation_error.h>
#include <validation/core_block_connection_context.h>
#include <validation/core_block_connection_setup.h>
#include <validation/core_chain_validation_context.h>
#include <validation_state.h>
#include <validationinterface.h>
#include <util/check.h>
#include <util/log.h>
#include <util/translation.h>

#include <algorithm>
#include <cassert>
#include <ranges>
#include <utility>

using kernel::Notifications;

namespace {

ExternalCacheUsage ExternalCacheUsageForEvents(const ChainstateEventSink* chain_events)
{
    return chain_events ? chain_events->CacheUsage() : ExternalCacheUsage{};
}

std::shared_ptr<const CBlock> LoadBlockForConnection(
    Notifications& notifications,
    BlockValidationState& state,
    CBlockIndex& block_index,
    std::shared_ptr<const CBlock> cached_block,
    BlockDataReader& block_reader) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    if (cached_block) {
        LogDebug(BCLog::BENCH, "  - Using cached block\n");
        return cached_block;
    }

    std::shared_ptr<CBlock> block{std::make_shared<CBlock>()};
    if (!block_reader.ReadBlock(*block, block_index)) {
        FatalError(notifications, state, _("Failed to read block."));
        return nullptr;
    }
    return block;
}

CoreBlockConnectionRuntimeInputs MakeCoreBlockConnectionRuntimeInputs(
    CoreChainValidationContext& context,
    BlockUndoWriter& undo_writer,
    BlockIndexValidityCommitter& block_index_committer) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    return {
        .notifications = context.Notifications(),
        .undo_writer = undo_writer,
        .block_index_committer = block_index_committer,
        .script_check_queue = context.ScriptCheckQueue(),
        .validation_cache = context.ScriptValidationCache(),
        .trace_counters = context.BlockConnectionTraceCounters(),
    };
}

bool RunBlockConnection(
    BlockValidationState& state,
    CBlockIndex& block_index,
    const std::shared_ptr<const CBlock>& block,
    validation::BlockConnectionState& connection_state,
    CoreBlockConnectionRuntimeInputs runtime_inputs,
    CoreBlockConnectionPlan connection_plan,
    std::optional<const char*>& last_reason_logged,
    ValidationSignals* signals) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    CoreBlockConnectionSetup connection_setup{
        runtime_inputs,
        std::move(connection_plan),
        block_index,
        /*cache_script_results=*/false};
    connection_setup.MaybeLogScriptPolicy(last_reason_logged, block->GetHash());
    const validation::BlockConnectionRequest request{connection_setup.Request(*block, connection_state)};
    const validation::BlockConnectionResult connection_result{validation::BlockConnectionEngine{}.Connect(request, state)};
    if (signals) {
        signals->BlockChecked(block, state);
    }
    if (!connection_result.Succeeded()) {
        LogError("%s: Block connection %s failed, %s\n", "ConnectTip", block_index.GetBlockHash().ToString(), state.ToString());
    }
    return connection_result.Succeeded();
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

} // namespace

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

bool CoreChainActivationState::DisconnectTip(BlockValidationState& state, ChainstateEventSink* chain_events) const
{
    return m_chainstate.DisconnectTip(state, chain_events);
}

void CoreChainActivationState::PruneBlockIndexCandidates() const
{
    m_chainstate.PruneBlockIndexCandidates();
}

void CoreChainActivationState::MarkInvalidChainFound(CBlockIndex& block_index) const
{
    m_chainstate.InvalidChainFound(&block_index);
}

void CoreChainActivationState::NotifyReorgCompleted(ChainstateEventSink* chain_events, bool success) const
{
    if (chain_events) chain_events->ReorgCompleted(m_chainstate, success);
}

void CoreChainActivationState::CheckPostReorgState(ChainstateEventSink* chain_events) const
{
    if (!chain_events) return;

    chain_events->CheckPostReorgState(m_chainstate.CoinsTip(), m_chainstate.m_chain.Height() + 1);
}

void CoreChainActivationState::CheckForkWarningConditions() const
{
    m_chainstate.CheckForkWarningConditions();
}

CoreConnectTipResult ConnectCoreChainTip(CoreConnectTipRequest request, BlockValidationState& state)
{
    AssertLockHeld(cs_main);
    CoreConnectTipResources& resources{request.resources};
    assert(request.block_index.pprev == resources.context.ActiveTip());

    const auto time_start{SteadyClock::now()};
    std::shared_ptr<const CBlock> block_to_connect{LoadBlockForConnection(
        resources.context.Notifications(),
        state,
        request.block_index,
        std::move(request.cached_block),
        resources.block_reader)};
    if (!block_to_connect) return CoreConnectTipResult::BlockReadFailed();

    const auto time_block_loaded{SteadyClock::now()};
    LogDebug(BCLog::BENCH, "  - Load block from disk: %.2fms\n",
             Ticks<MillisecondsDouble>(time_block_loaded - time_start));

    SteadyClock::time_point time_block_connected;
    {
        const std::unique_ptr<validation::BlockConnectionAttemptGuard> connection_attempt{resources.connection_state.BeginConnectionAttempt()};
        if (!RunBlockConnection(
                state,
                request.block_index,
                block_to_connect,
                resources.connection_state,
                MakeCoreBlockConnectionRuntimeInputs(resources.context, resources.undo_writer, resources.block_index_committer),
                PlanCoreBlockConnection(SnapshotCoreBlockConnectionPolicy(resources.context, request.block_index), resources.block_index_lookup, request.block_index),
                resources.last_script_check_reason_logged,
                resources.signals)) {
            if (state.IsInvalid()) {
                resources.context.MarkInvalidBlockFound(request.block_index, state);
            }
            return CoreConnectTipResult::BlockConnectionFailed();
        }

        time_block_connected = SteadyClock::now();
        AccumulateAndLogConnectTipStep("Connect total", time_block_connected - time_block_loaded, resources.timing.time_connect_total, resources.timing.blocks_total);
        connection_attempt->Commit();
    }

    const auto time_coins_committed{SteadyClock::now()};
    AccumulateAndLogConnectTipStep("Flush", time_coins_committed - time_block_connected, resources.timing.time_flush, resources.timing.blocks_total);

    if (!resources.context.FlushActiveChainstateIfNeeded(state, ExternalCacheUsageForEvents(resources.chain_events))) {
        return CoreConnectTipResult::ChainstateFlushFailed();
    }

    const auto time_chainstate_persisted{SteadyClock::now()};
    AccumulateAndLogConnectTipStep("Writing chainstate", time_chainstate_persisted - time_coins_committed, resources.timing.time_chainstate, resources.timing.blocks_total);

    PublishConnectedBlock(resources.chain_events, *block_to_connect, request.block_index.nHeight);

    resources.context.AdvanceActiveChainTip(request.block_index, resources.chain_events);

    const auto time_tip_advanced{SteadyClock::now()};
    AccumulateAndLogConnectTipStep("Connect postprocess", time_tip_advanced - time_chainstate_persisted, resources.timing.time_post_connect, resources.timing.blocks_total);
    AccumulateAndLogConnectTipTotal(time_tip_advanced - time_start, resources.timing.time_total, resources.timing.blocks_total);

    resources.connected_blocks.emplace_back(&request.block_index, std::move(block_to_connect));
    return CoreConnectTipResult::Connected();
}

CoreActivateBestChainStepResult ActivateCoreBestChainStep(CoreActivateBestChainStepRequest request, BlockValidationState& state)
{
    AssertLockHeld(cs_main);
    CoreChainActivationState& active_chain{request.active_chain};

    const CBlockIndex* old_tip{active_chain.Tip()};
    const CBlockIndex* fork{active_chain.FindFork(request.index_most_work)};

    bool blocks_disconnected{false};
    while (active_chain.Tip() && active_chain.Tip() != fork) {
        if (!active_chain.DisconnectTip(state, request.connection.chain_events)) {
            // This is likely a fatal error. Notify the event sink without
            // restoring disconnected transactions, just in case observers run
            // before shutdown.
            active_chain.NotifyReorgCompleted(request.connection.chain_events, /*success=*/false);

            // If we're unable to disconnect a block during normal operation,
            // then that is a failure of our local system -- we should abort
            // rather than stay on a less work chain.
            FatalError(active_chain.Notifications(), state, _("Failed to disconnect block."));
            return CoreActivateBestChainStepResult::SystemError();
        }
        blocks_disconnected = true;
    }

    std::vector<CBlockIndex*> blocks_to_connect;
    bool continue_step{true};
    auto result{CoreActivateBestChainStepResult::Completed()};
    int height{fork ? fork->nHeight : -1};
    while (continue_step && height != request.index_most_work.nHeight) {
        // Don't iterate the entire list of potential improvements toward the
        // best tip, as we likely only need a few blocks along the way.
        const int target_height{std::min(height + 32, request.index_most_work.nHeight)};
        blocks_to_connect.clear();
        blocks_to_connect.reserve(target_height - height);
        CBlockIndex* block_index{request.index_most_work.GetAncestor(target_height)};
        while (block_index && block_index->nHeight != height) {
            blocks_to_connect.push_back(block_index);
            block_index = block_index->pprev;
        }
        height = target_height;

        for (CBlockIndex* block_to_connect : blocks_to_connect | std::views::reverse) {
            const CoreConnectTipResult connect_result{ConnectCoreChainTip(
                {
                    .resources = request.connection,
                    .block_index = *block_to_connect,
                    .cached_block = block_to_connect == &request.index_most_work ? request.cached_best_block : std::shared_ptr<const CBlock>{},
                },
                state)};
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
                active_chain.NotifyReorgCompleted(request.connection.chain_events, /*success=*/false);
                return CoreActivateBestChainStepResult::SystemError();
            }

            active_chain.PruneBlockIndexCandidates();
            if (!old_tip || active_chain.Tip()->nChainWork > old_tip->nChainWork) {
                // We're in a better position than we were. Return temporarily
                // to release the lock.
                continue_step = false;
                break;
            }
        }
    }

    if (blocks_disconnected) {
        active_chain.NotifyReorgCompleted(request.connection.chain_events, /*success=*/true);
    }
    active_chain.CheckPostReorgState(request.connection.chain_events);
    active_chain.CheckForkWarningConditions();

    return result;
}
