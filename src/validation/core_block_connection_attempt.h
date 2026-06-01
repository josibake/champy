// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_BLOCK_CONNECTION_ATTEMPT_H
#define BITCOIN_CORE_BLOCK_CONNECTION_ATTEMPT_H

#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_spend.h>

class CBlock;

class CoreBlockConnectionAttempt final {
public:
    CoreBlockConnectionAttempt(
        const CBlock& block,
        Consensus::BlockSpendWorkspace& spend_workspace,
        Consensus::BlockConsensusContext consensus_context,
        Consensus::BlockSpendConsensusOptions spend_options);

    CoreBlockConnectionAttempt(const CoreBlockConnectionAttempt&) = delete;
    CoreBlockConnectionAttempt& operator=(const CoreBlockConnectionAttempt&) = delete;
    CoreBlockConnectionAttempt(CoreBlockConnectionAttempt&&) = delete;
    CoreBlockConnectionAttempt& operator=(CoreBlockConnectionAttempt&&) = delete;

    [[nodiscard]] Consensus::BlockSpendResult<Consensus::BlockSpendEffects> ValidateAndStageSpend(Consensus::BlockScriptChecker& script_checker);
    [[nodiscard]] Consensus::BlockSpendResult<Consensus::BlockSpendEffects> ValidateAndStageSpend(const Consensus::BlockSpendJoiner& joiner, Consensus::BlockScriptChecker& script_checker);
    [[nodiscard]] Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CompleteSpendStage(
        Consensus::BlockSpendResult<Consensus::BlockSpendEffects> spend_effects,
        Consensus::BlockScriptChecker& script_checker);

private:
    Consensus::BlockSpendWorkspace& m_spend_workspace;
    Consensus::BlockConsensusPipeline m_pipeline;
    Consensus::BlockSpendConsensusOptions m_spend_options;
};

#endif // BITCOIN_CORE_BLOCK_CONNECTION_ATTEMPT_H
