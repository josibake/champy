// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_block_connection_attempt.h>

#include <coins_view_spend_state.h>
#include <core_block_commit_adapters.h>

#include <utility>

class CoreBlockConnectionAttempt::Impl final {
public:
    Impl(
        const CBlock& block,
        CBlockIndex& block_index,
        CCoinsViewCache& view,
        node::BlockManager& blockman,
        std::set<CBlockIndex*>& dirty_blockindex,
        Consensus::BlockConsensusContext consensus_context,
        Consensus::BlockSpendConsensusOptions spend_options)
        : m_spend_workspace{view, block_index},
          m_commit_context{consensus_context.commit},
          m_pipeline{block, consensus_context},
          m_spend_state_committer{m_spend_workspace.StagedCoins(), view},
          m_effects_writer{blockman, dirty_blockindex, view, block_index},
          m_spend_options{spend_options}
    {
    }

    [[nodiscard]] Consensus::BlockSpendResult<Consensus::BlockSpendEffects> ValidateAndStageSpend(Consensus::BlockScriptChecker& script_checker)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        return m_pipeline.ValidateAndStageSpend(m_spend_workspace, script_checker, m_spend_options);
    }

    [[nodiscard]] Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CompleteSpendStage(
        Consensus::BlockSpendResult<Consensus::BlockSpendEffects> spend_effects,
        Consensus::BlockScriptChecker& script_checker)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        return m_pipeline.CompleteSpendStage(std::move(spend_effects), script_checker);
    }

    [[nodiscard]] Consensus::BlockCommitResult<void> WriteUndoAndCommitSpendState(const Consensus::BlockSpendEffects& effects)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        if (const auto undo_write{m_effects_writer.WriteBlockRevertData(m_commit_context, effects)}; !undo_write) {
            return Consensus::Unexpected<Consensus::BlockCommitError>{undo_write.error()};
        }

        return m_spend_state_committer.CommitSpendState(m_commit_context, effects);
    }

    [[nodiscard]] Consensus::BlockCommitResult<void> CommitBlockIndex(const Consensus::BlockSpendEffects& effects)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        return m_effects_writer.CommitBlockMetadata(m_commit_context, effects);
    }

private:
    Consensus::CoinsViewBlockSpendWorkspace m_spend_workspace;
    Consensus::BlockCommitContext m_commit_context;
    Consensus::BlockConsensusPipeline m_pipeline;
    CoreBlockSpendStateCommitter m_spend_state_committer;
    CoreBlockEffectsWriter m_effects_writer;
    Consensus::BlockSpendConsensusOptions m_spend_options;
};

CoreBlockConnectionAttempt::CoreBlockConnectionAttempt(
    const CBlock& block,
    CBlockIndex& block_index,
    CCoinsViewCache& view,
    node::BlockManager& blockman,
    std::set<CBlockIndex*>& dirty_blockindex,
    Consensus::BlockConsensusContext consensus_context,
    Consensus::BlockSpendConsensusOptions spend_options)
    : m_impl{std::make_unique<Impl>(
          block,
          block_index,
          view,
          blockman,
          dirty_blockindex,
          consensus_context,
          spend_options)}
{
}

CoreBlockConnectionAttempt::~CoreBlockConnectionAttempt() = default;
CoreBlockConnectionAttempt::CoreBlockConnectionAttempt(CoreBlockConnectionAttempt&&) noexcept = default;
CoreBlockConnectionAttempt& CoreBlockConnectionAttempt::operator=(CoreBlockConnectionAttempt&&) noexcept = default;

Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CoreBlockConnectionAttempt::ValidateAndStageSpend(Consensus::BlockScriptChecker& script_checker)
{
    return m_impl->ValidateAndStageSpend(script_checker);
}

Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CoreBlockConnectionAttempt::CompleteSpendStage(
    Consensus::BlockSpendResult<Consensus::BlockSpendEffects> spend_effects,
    Consensus::BlockScriptChecker& script_checker)
{
    return m_impl->CompleteSpendStage(std::move(spend_effects), script_checker);
}

Consensus::BlockCommitResult<void> CoreBlockConnectionAttempt::WriteUndoAndCommitSpendState(const Consensus::BlockSpendEffects& effects)
{
    return m_impl->WriteUndoAndCommitSpendState(effects);
}

Consensus::BlockCommitResult<void> CoreBlockConnectionAttempt::CommitBlockIndex(const Consensus::BlockSpendEffects& effects)
{
    return m_impl->CommitBlockIndex(effects);
}
