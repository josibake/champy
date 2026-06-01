// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_connection.h>

#include <chain.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/params.h>
#include <kernel/notifications_interface.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>
#include <util/log.h>
#include <util/trace.h>
#include <util/translation.h>
#include <validation/block_connection_trace.h>
#include <validation/block_connection_state.h>
#include <validation/block_validation.h>
#include <validation/block_validation_error.h>
#include <validation/block_validation_policy.h>
#include <validation/core_block_commit_adapters.h>
#include <validation/core_block_connection_attempt.h>
#include <validation_state.h>

#include <cassert>
#include <utility>

TRACEPOINT_SEMAPHORE(validation, block_connected);

namespace validation {

namespace {

[[nodiscard]] BlockConnectionResult CommitBlockConnectionEffects(const BlockConnectionCommitRequest& request, BlockConnectionCommitPackage package, BlockValidationState& state)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    AssertLockHeld(cs_main);

    const BlockConnectionCommitRuntime& runtime{request.runtime};
    const BlockConnectionCommitContext& context{request.context};
    CBlockIndex& block_index{context.block_index};
    BlockConnectionState& connection_state{context.connection_state};
    BlockConnectionTrace& trace{runtime.trace};
    const CBlock& block{context.block};
    [[maybe_unused]] const uint256 block_hash{block.GetHash()};

    if (connection_state.BestBlock() != package.expected_previous_block) {
        state.Error("stale block connection");
        return BlockConnectionResult::Failed();
    }

    if (!package.effects) {
        connection_state.SetBestBlock(block_index.GetBlockHash());
        return BlockConnectionResult::Connected();
    }

    assert(package.spend_state);
    CoreBlockEffectsWriter effects_writer{
        runtime.undo_writer,
        runtime.block_index_committer,
        connection_state,
        block_index};
    const auto commit{Consensus::CommitBlockEffects(
        package.commit_context,
        *package.effects,
        effects_writer,
        package.spend_state->Committer(),
        effects_writer)};
    if (!commit) {
        ApplyBlockCommitError(state, commit.error());
        return BlockConnectionResult::Failed();
    }

    trace.UndoWritten();
    trace.IndexCommitted();

    TRACEPOINT(validation, block_connected,
               block_hash.data(),
               block_index.nHeight,
               block.vtx.size(),
               package.effects->inputs,
               package.effects->sigop_cost,
               trace.TraceDuration().count());

    return BlockConnectionResult::Connected();
}

} // namespace

BlockConnectionResult BlockConnectionEngine::Connect(const BlockConnectionRequest& request, BlockValidationState& state) const
{
    AssertLockHeld(cs_main);

    const BlockConnectionRuntime& runtime{request.runtime};
    const BlockConnectionContext& context{request.context};
    const CBlock& block{request.block};
    CBlockIndex& block_index{request.block_index};
    BlockConnectionState& connection_state{request.connection_state};
    const BlockConnectionOptions& options{request.options};

    const uint256 block_hash{block.GetHash()};
    assert(*block_index.phashBlock == block_hash);

    BlockConnectionTrace& trace{runtime.trace};
    const Consensus::Params& consensus_params{context.consensus_params};

    // Check it again in case a previous version let a bad block in.
    if (!CheckBlock(block, state, consensus_params, options.block_check_options)) {
        if (state.GetResult() == BlockValidationResult::BLOCK_MUTATED) {
            // We don't write down blocks to disk if they may have been
            // corrupted, so this should be impossible unless we're having hardware
            // problems.
            const bilingual_str message = _("Corrupt block found indicating potential hardware failure.");
            runtime.notifications.fatalError(message);
            state.Error(message.original);
            return BlockConnectionResult::Failed();
        }
        LogError("%s: Consensus::CheckBlock: %s\n", __func__, state.ToString());
        return BlockConnectionResult::Failed();
    }

    // Verify that the view's current state corresponds to the previous block.
    const uint256 hashPrevBlock{block_index.pprev == nullptr ? uint256{} : block_index.pprev->GetBlockHash()};
    assert(hashPrevBlock == connection_state.BestBlock());

    trace.CountBlock();

    // Special case for the genesis block, skipping connection of its
    // transactions. Its coinbase is unspendable.
    if (block_hash == consensus_params.hashGenesisBlock) {
        return BlockConnectionResult::Connected(BlockConnectionCommitPackage{
            .expected_previous_block = hashPrevBlock,
            .commit_context = context.consensus_context.commit,
            .spend_state = nullptr,
            .effects = std::nullopt,
        });
    }

    trace.SanityChecksDone();
    trace.ForkChecksDone();

    assert(context.sequence_lock_times);
    auto spend_state{connection_state.BeginBlockSpend(context.consensus_context.spend, context.sequence_lock_times)};
    if (!spend_state) {
        ApplyBlockSpendError(state, spend_state.error());
        return BlockConnectionResult::Failed();
    }

    BlockConnectionSpendState& block_spend{**spend_state};
    CoreBlockConnectionAttempt connection_attempt{
        block,
        block_spend.Workspace(),
        context.consensus_context,
        context.spend_options};
    auto spend_effects{connection_attempt.ValidateAndStageSpend(runtime.script_checker)};
    const int spend_inputs{spend_effects ? spend_effects->inputs : 0};
    trace.SpendStageValidated(block.vtx.size(), spend_inputs);

    // Complete any queued script work before leaving the spend stage so cache
    // updates and script diagnostics stay inside the script-checker boundary.
    spend_effects = connection_attempt.CompleteSpendStage(std::move(spend_effects), runtime.script_checker);
    if (!spend_effects) {
        ApplyBlockSpendError(state, spend_effects.error());
        LogInfo("Block validation error: %s", state.ToString());
        return BlockConnectionResult::Failed();
    }
    assert(spend_effects);
    trace.SpendStageCompleted(spend_effects->inputs);

    return BlockConnectionResult::Connected(BlockConnectionCommitPackage{
        .expected_previous_block = hashPrevBlock,
        .commit_context = context.consensus_context.commit,
        .spend_state = std::move(*spend_state),
        .effects = std::move(*spend_effects),
    });
}

BlockConnectionResult BlockConnectionEngine::Commit(const BlockConnectionCommitRequest& request, BlockConnectionCommitPackage package, BlockValidationState& state) const
{
    return CommitBlockConnectionEffects(request, std::move(package), state);
}

} // namespace validation
