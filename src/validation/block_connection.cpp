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
        if (options.commit) {
            connection_state.SetBestBlock(block_index.GetBlockHash());
        }
        return BlockConnectionResult::Connected();
    }

    BlockUndoWriter& undo_writer{runtime.undo_writer};
    BlockIndexValidityCommitter& block_index_committer{runtime.block_index_committer};

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
        block_index,
        undo_writer,
        block_index_committer,
        connection_state,
        block_spend.Workspace(),
        block_spend.Committer(),
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
    const Consensus::BlockSpendEffects& effects{*spend_effects};
    trace.SpendStageCompleted(effects.inputs);

    if (!options.commit) {
        return BlockConnectionResult::Connected();
    }

    if (const auto spend_state_commit{connection_attempt.WriteUndoAndCommitSpendState(effects)}; !spend_state_commit) {
        ApplyBlockCommitError(state, spend_state_commit.error());
        return BlockConnectionResult::Failed();
    }

    trace.UndoWritten();

    if (const auto index_commit{connection_attempt.CommitBlockIndex(effects)}; !index_commit) {
        ApplyBlockCommitError(state, index_commit.error());
        return BlockConnectionResult::Failed();
    }

    trace.IndexCommitted();

    TRACEPOINT(validation, block_connected,
               block_hash.data(),
               block_index.nHeight,
               block.vtx.size(),
               effects.inputs,
               effects.sigop_cost,
               trace.TraceDuration().count());

    return BlockConnectionResult::Connected();
}

} // namespace validation
