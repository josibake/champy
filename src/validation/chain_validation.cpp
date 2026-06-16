// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/chain_validation.h>

#include <chainstate.h>
#include <consensus/segment_spend.h>
#include <consensus/spend_state_batch.h>
#include <validation/block_header_context_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_script_check_adapters.h>
#include <validation/block_validation_error.h>
#include <validation/block_validation_internal.h>
#include <validation/coins_view_spend_state.h>
#include <validation/core_block_connection_context.h>
#include <validation/core_chain_activation.h>
#include <validation/core_chain_lock.h>
#include <validation/core_chain_validation_runtimes.h>

#include <array>
#include <cassert>
#include <span>
#include <utility>

namespace {

Consensus::SegmentBlockView BuildAcceptedTipSegmentBlockView(
    const NewBlockCandidateContextSnapshot& context,
    const CBlock& block,
    const Consensus::BlockSpendConsensusOptions& spend_options)
{
    return {
        .context = {
            .hash = context.block.hash,
            .parent_hash = context.block.parent_hash,
            .height = context.block.height,
            .previous_median_time_past = context.previous_median_time_past,
            .spend_options = spend_options,
            .block_subsidy = context.block_subsidy,
        },
        .transactions = block.vtx,
    };
}

bool CompleteCoreBlockScriptChecks(
    Consensus::BlockScriptChecker& script_checker,
    std::span<const Consensus::TransactionScriptCheckPlan> checks,
    BlockValidationState& state)
{
    for (const Consensus::TransactionScriptCheckPlan& check : checks) {
        auto submitted{script_checker.Check(check)};
        if (!submitted) {
            ApplyBlockSpendError(state, submitted.error());
            return false;
        }
    }

    auto completed{script_checker.Complete()};
    if (!completed) {
        ApplyBlockSpendError(state, completed.error());
        return false;
    }
    return true;
}

} // namespace

NewBlockHeadersResult ProcessNewBlockHeaders(ProcessNewBlockHeadersRequest request)
{
    CoreHeaderAdmissionRuntime runtime{request.chainman};
    return ::ProcessNewBlockHeaders(runtime, request.headers, request.options, request.time, request.state);
}

NewBlockStructuralCheckResult CheckNewBlockStructural(CheckNewBlockStructuralRequest request)
{
    return ::CheckNewBlockStructural(request.chainman.GetConsensus(), request.block, request.state);
}

BlockAcceptanceResult AcceptBlock(AcceptBlockRequest request)
{
    CoreBlockDataAdmissionRuntime runtime{request.chainman};
    return ::AcceptBlock(runtime, request.block, request.state, request.options, request.time);
}

BlockAcceptanceResult AcceptNewBlockData(AcceptNewBlockDataRequest request)
{
    CoreBlockDataAdmissionRuntime runtime{request.chainman};
    return ::AcceptNewBlockData(runtime, request.block, request.state, request.options, request.time);
}

std::optional<NewBlockCandidateContextSnapshot> SnapshotAcceptedBlockContext(
    SnapshotAcceptedBlockContextRequest request)
{
    CoreAcceptedContextReader reader{request.chainman};
    return ::SnapshotAcceptedBlockContext(reader, request.block_hash);
}

std::optional<CoreBlockConnectionCommitWork> PrepareAcceptedTipCommitWork(
    PrepareAcceptedTipCommitWorkRequest request)
{
    AssertLockNotHeld(cs_main);
    assert(request.block);
    if (!request.context.Matches(*request.block)) {
        request.state.Error("accepted block context mismatch");
        return std::nullopt;
    }

    CoreActivationRuntime runtime{request.chainman};

    WAIT_LOCK(cs_main, chain_lock_handle);
    CoreChainLock chain_lock{chain_lock_handle};

    CoreBlockIndexStore block_index{request.chainman};
    CBlockIndex* block_index_entry{block_index.LookupBlockIndex(request.context.block.hash)};
    if (!block_index_entry) return std::nullopt;
    if (request.chainman.ActiveTip() != block_index_entry->pprev) return std::nullopt;
    if (!request.context.has_spend_stage) return std::nullopt;

    CoreBlockConnectionPlan connection_plan{PlanCoreBlockConnection(
        runtime.SnapshotConnectionPolicy(*block_index_entry),
        block_index,
        *block_index_entry)};
    MaybeLogCoreBlockConnectionScriptPolicy(
        request.chainman.ActiveChainstate().LastScriptCheckReasonLogged(),
        *block_index_entry,
        request.block->GetHash(),
        connection_plan);
    assert(connection_plan.context.sequence_lock_times);

    validation::CoinsViewSpendState spend_state{request.chainman.ActiveChainstate().CoinsTip()};
    Consensus::SpendLookupBatchBackendAdapter batch_spend_state{spend_state};
    Consensus::SegmentSpendBatchViewAdapter segment_spend_state{
        batch_spend_state,
        *connection_plan.context.sequence_lock_times};
    CoreBlockScriptChecks script_checks{
        runtime.ScriptTaskExecutor(),
        connection_plan.script_check_decision.run_script_checks,
        /*cache_results=*/false,
        runtime.ScriptValidationCache(),
        &chain_lock};

    const Consensus::SegmentBlockView segment_block{BuildAcceptedTipSegmentBlockView(
        request.context,
        *request.block,
        connection_plan.context.spend_options)};
    const std::array<Consensus::SegmentBlockView, 1> segment_blocks{segment_block};
    Consensus::SegmentSpentOutputJoin joined_inputs{Consensus::JoinSegmentSpentOutputs(
        segment_blocks,
        segment_spend_state)};
    const Consensus::ScriptCheckPlanCollection script_check_plans{
        script_checks.Checker().WantsChecks() ? Consensus::ScriptCheckPlanCollection::Collect : Consensus::ScriptCheckPlanCollection::Skip};

    auto block_stage{chain_lock.RunUnlocked([&]() {
        return Consensus::ValidateResolvedSegmentBlockSpend(
            segment_block,
            joined_inputs,
            /*block_index=*/0,
            script_check_plans);
    })};
    if (!block_stage) {
        ApplyBlockSpendError(request.state, block_stage.error());
        return std::nullopt;
    }

    if (!CompleteCoreBlockScriptChecks(script_checks.Checker(), block_stage->script_checks, request.state)) {
        return std::nullopt;
    }

    return MakeCoreBlockConnectionCommitWork(
        request.context.block,
        request.block,
        validation::BlockConnectionCommitPackage{
            .expected_previous_block = request.context.previous_block_hash,
            .commit_context = connection_plan.context.consensus_context.commit,
            .effects = std::move(block_stage->effects),
        },
        runtime.TraceCounters());
}

BlockActivationResult ActivateAcceptedBlock(ActivateAcceptedBlockRequest request)
{
    CoreActivationRuntime runtime{request.chainman};
    return ::ActivateAcceptedBlock(runtime, request.chain_events, request.block, request.state, request.time);
}

BlockActivationResult ActivateAcceptedTipCandidate(ActivateAcceptedTipCandidateRequest request)
{
    CoreActivationRuntime runtime{request.chainman};
    return ::ActivateAcceptedTipCandidate(runtime, request.chain_events, request.block, request.state, request.time);
}

BlockActivationResult CommitAcceptedTipCandidate(CommitAcceptedTipCandidateRequest request)
{
    CoreActivationRuntime runtime{request.chainman};
    (void)runtime.NotifyHeaderTip();
    return request.chainman.ActiveChainstate().CommitMostWorkTipBlock(
        request.state,
        request.time.CurrentTime(),
        std::move(request.work),
        request.chain_events);
}

void ReportBlockChecked(ReportBlockCheckedRequest request)
{
    CoreActivationRuntime runtime{request.chainman};
    ::ReportBlockChecked(runtime.ValidationEvents(), request.block, request.state);
}

NewBlockProcessingResult ProcessNewBlock(ProcessNewBlockRequest request)
{
    CoreBlockDataAdmissionRuntime admission_runtime{request.chainman};
    CoreAcceptedContextReader context_reader{request.chainman};
    CoreActivationRuntime activation_runtime{request.chainman};
    return ::ProcessNewBlock(admission_runtime, context_reader, activation_runtime, request.chain_events, request.block, request.options, request.time);
}

BlockValidationState TestActiveBlockValidity(TestActiveBlockValidityRequest request)
{
    LOCK(::cs_main);
    return TestActiveBlockValidityLocked(request);
}

BlockValidationState TestActiveBlockValidityLocked(TestActiveBlockValidityRequest request)
{
    return ::TestBlockValidity(request.chainman.ActiveChainstate(), request.block, request.options, request.time);
}
