// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_context.h>

#include <chain.h>
#include <chainstate.h>
#include <consensus/block_spend.h>
#include <consensus/params.h>
#include <validation/block_header_context_adapters.h>

CoreBlockConnectionPlan PlanCoreBlockConnection(ChainstateManager& chainman, BlockIndexStore& block_index_store, const CBlockIndex& block_index)
{
    const Consensus::Params& consensus_params{chainman.GetParams().GetConsensus()};
    const bool has_spend_stage{block_index.pprev != nullptr};
    CoreBlockScriptCheckDecision script_check_decision{.run_script_checks = true};
    if (has_spend_stage) {
        script_check_decision = DetermineCoreBlockScriptChecks(chainman, block_index_store, block_index, consensus_params);
    }

    return {
        .context = {
            .consensus_params = consensus_params,
            .consensus_context = BuildCoreBlockConsensusContext(block_index, chainman, consensus_params),
            .spend_options = has_spend_stage ? BuildCoreBlockSpendConsensusOptions(block_index, chainman) : Consensus::BlockSpendConsensusOptions{},
        },
        .script_check_decision = script_check_decision,
        .has_spend_stage = has_spend_stage,
    };
}

void MaybeLogCoreBlockConnectionScriptPolicy(std::optional<const char*>& last_reason_logged, const CBlockIndex& block_index, const uint256& block_hash, const CoreBlockConnectionPlan& plan)
{
    if (!plan.has_spend_stage) return;
    MaybeLogCoreBlockScriptCheckDecision(last_reason_logged, block_index, block_hash, plan.script_check_decision);
}
