// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_attempt.h>

#include <utility>

CoreBlockConnectionAttempt::CoreBlockConnectionAttempt(
    const CBlock& block,
    Consensus::SpendWorkspace& spend_workspace,
    Consensus::BlockConsensusContext consensus_context,
    Consensus::BlockSpendConsensusOptions spend_options)
    : m_spend_workspace{spend_workspace},
      m_pipeline{block, consensus_context},
      m_spend_options{spend_options}
{
}

Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CoreBlockConnectionAttempt::ValidateAndStageSpend(Consensus::BlockScriptChecker& script_checker)
{
    return m_pipeline.ValidateAndStageSpend(m_spend_workspace, script_checker, m_spend_options);
}

Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CoreBlockConnectionAttempt::ValidateAndStageSpend(const Consensus::BlockSpendJoiner& joiner, Consensus::BlockScriptChecker& script_checker)
{
    return m_pipeline.ValidateAndStageSpend(m_spend_workspace, joiner, script_checker, m_spend_options);
}

Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CoreBlockConnectionAttempt::CompleteSpendStage(
    Consensus::BlockSpendResult<Consensus::BlockSpendEffects> spend_effects,
    Consensus::BlockScriptChecker& script_checker)
{
    return m_pipeline.CompleteSpendStage(std::move(spend_effects), script_checker);
}
