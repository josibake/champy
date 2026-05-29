// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_connection.h>

#include <chainstate.h>
#include <consensus/block_consensus_pipeline.h>
#include <kernel/chainparams.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>
#include <util/log.h>
#include <util/trace.h>
#include <util/translation.h>
#include <validation/block_data_adapters.h>
#include <validation/block_header_context_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_script_check_adapters.h>
#include <validation/block_validation.h>
#include <validation/block_validation_error.h>
#include <validation/block_validation_policy.h>
#include <validation/coins_view_spend_state.h>
#include <validation/connect_block_bench.h>
#include <validation/core_block_commit_adapters.h>
#include <validation/core_block_connection_attempt.h>
#include <validation/core_block_policy.h>
#include <validation_state.h>

#include <cassert>
#include <utility>

TRACEPOINT_SEMAPHORE(validation, block_connected);

namespace validation {

bool BlockConnectionEngine::Connect(const BlockConnectionRequest& request, BlockValidationState& state) const
{
    AssertLockHeld(cs_main);

    Chainstate& chainstate{request.chainstate};
    const CBlock& block{request.block};
    CBlockIndex& block_index{request.block_index};
    CCoinsViewCache& view{request.coins_view};
    const ConnectBlockOptions& options{request.options};

    const uint256 block_hash{block.GetHash()};
    assert(*block_index.phashBlock == block_hash);

    ConnectBlockBench bench{chainstate.m_chainman};
    const CChainParams& params{chainstate.m_chainman.GetParams()};

    // Check it again in case a previous version let a bad block in.
    if (!CheckBlock(block, state, params.GetConsensus(), options.block_check_options)) {
        if (state.GetResult() == BlockValidationResult::BLOCK_MUTATED) {
            // We don't write down blocks to disk if they may have been
            // corrupted, so this should be impossible unless we're having hardware
            // problems.
            return FatalError(chainstate.m_chainman.GetNotifications(), state, _("Corrupt block found indicating potential hardware failure."));
        }
        LogError("%s: Consensus::CheckBlock: %s\n", __func__, state.ToString());
        return false;
    }

    // Verify that the view's current state corresponds to the previous block.
    const uint256 hashPrevBlock{block_index.pprev == nullptr ? uint256{} : block_index.pprev->GetBlockHash()};
    assert(hashPrevBlock == view.GetBestBlock());

    bench.CountBlock();

    // Special case for the genesis block, skipping connection of its
    // transactions. Its coinbase is unspendable.
    if (block_hash == params.GetConsensus().hashGenesisBlock) {
        if (options.commit) {
            view.SetBestBlock(block_index.GetBlockHash());
        }
        return true;
    }

    const CoreBlockScriptCheckDecision script_check_decision{DetermineCoreBlockScriptChecks(chainstate, block_index, params.GetConsensus())};

    bench.SanityChecksDone();

    const Consensus::BlockSpendConsensusOptions spend_options{BuildCoreBlockSpendConsensusOptions(
        block_index,
        chainstate.m_chainman)};

    bench.ForkChecksDone();

    MaybeLogCoreBlockScriptCheckDecision(chainstate, block_index, block_hash, script_check_decision);

    CoreBlockScriptChecks script_checks{
        chainstate.m_chainman.GetCheckQueue(),
        script_check_decision.run_script_checks,
        options.cache_script_results,
        chainstate.m_chainman.m_validation_cache};
    CoreBlockDataStore block_store{chainstate.m_blockman};
    CoreBlockIndexStore block_index_store{chainstate.m_chainman};
    const Consensus::BlockConsensusContext consensus_context{BuildCoreBlockConsensusContext(block_index, chainstate.m_chainman, params.GetConsensus())};
    Consensus::CoinsViewBlockSpendWorkspace spend_workspace{view, block_index};
    CoreBlockSpendStateCommitter spend_state_committer{spend_workspace.StagedCoins(), view};
    CoreBlockConnectionAttempt connection_attempt{
        block,
        block_index,
        view,
        block_store,
        block_index_store,
        spend_workspace,
        spend_state_committer,
        consensus_context,
        spend_options};
    auto spend_effects{connection_attempt.ValidateAndStageSpend(script_checks.Checker())};
    const int spend_inputs{spend_effects ? spend_effects->inputs : 0};
    bench.SpendStageValidated(block.vtx.size(), spend_inputs);

    // Complete any queued script work before leaving the spend stage so cache
    // updates and script diagnostics stay inside the script-checker boundary.
    spend_effects = connection_attempt.CompleteSpendStage(std::move(spend_effects), script_checks.Checker());
    if (!spend_effects) {
        ApplyBlockSpendError(state, spend_effects.error());
        LogInfo("Block validation error: %s", state.ToString());
        return false;
    }
    assert(spend_effects);
    const Consensus::BlockSpendEffects& effects{*spend_effects};
    bench.SpendStageCompleted(effects.inputs);

    if (!options.commit) {
        return true;
    }

    if (const auto spend_state_commit{connection_attempt.WriteUndoAndCommitSpendState(effects)}; !spend_state_commit) {
        return ApplyBlockCommitError(state, spend_state_commit.error());
    }

    bench.UndoWritten();

    if (const auto index_commit{connection_attempt.CommitBlockIndex(effects)}; !index_commit) {
        return ApplyBlockCommitError(state, index_commit.error());
    }

    bench.IndexCommitted();

    TRACEPOINT(validation, block_connected,
               block_hash.data(),
               block_index.nHeight,
               block.vtx.size(),
               effects.inputs,
               effects.sigop_cost,
               bench.TraceDuration().count());

    return true;
}

} // namespace validation
