// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_attempt.h>

#include <utility>

CoreBlockConnectionAttempt::CoreBlockConnectionAttempt(
    const CBlock& block,
    CBlockIndex& block_index,
    BlockUndoWriter& undo_writer,
    BlockIndexValidityCommitter& block_index_committer,
    validation::BlockConnectionState& connection_state,
    Consensus::BlockSpendWorkspace& spend_workspace,
    Consensus::BlockSpendStateCommitter& spend_state_committer,
    Consensus::BlockConsensusContext consensus_context,
    Consensus::BlockSpendConsensusOptions spend_options)
    : m_spend_workspace{spend_workspace},
      m_commit_context{consensus_context.commit},
      m_pipeline{block, consensus_context},
      m_spend_state_committer{spend_state_committer},
      m_effects_writer{undo_writer, block_index_committer, connection_state, block_index},
      m_spend_options{spend_options}
{
}

Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CoreBlockConnectionAttempt::ValidateAndStageSpend(Consensus::BlockScriptChecker& script_checker)
{
    return m_pipeline.ValidateAndStageSpend(m_spend_workspace, script_checker, m_spend_options);
}

Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CoreBlockConnectionAttempt::CompleteSpendStage(
    Consensus::BlockSpendResult<Consensus::BlockSpendEffects> spend_effects,
    Consensus::BlockScriptChecker& script_checker)
{
    return m_pipeline.CompleteSpendStage(std::move(spend_effects), script_checker);
}

Consensus::BlockCommitResult<void> CoreBlockConnectionAttempt::WriteUndoAndCommitSpendState(const Consensus::BlockSpendEffects& effects)
{
    if (const auto undo_write{m_effects_writer.WriteBlockRevertData(m_commit_context, effects)}; !undo_write) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{undo_write.error()};
    }

    return m_spend_state_committer.CommitSpendState(m_commit_context, effects);
}

Consensus::BlockCommitResult<void> CoreBlockConnectionAttempt::CommitBlockIndex(const Consensus::BlockSpendEffects& effects)
{
    return m_effects_writer.CommitBlockMetadata(m_commit_context, effects);
}
