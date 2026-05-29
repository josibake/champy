// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_context.h>

#include <chain.h>
#include <chainstate.h>
#include <consensus/amount.h>
#include <consensus/block_spend.h>
#include <consensus/params.h>
#include <validation/block_header_context_adapters.h>

CoreBlockConnectionPolicySnapshot SnapshotCoreBlockConnectionPolicy(ChainstateManager& chainman, const CBlockIndex& block_index)
{
    const Consensus::Params& consensus_params{chainman.GetParams().GetConsensus()};
    return {
        .consensus_params = consensus_params,
        .header_context = BuildCoreBlockHeaderContext(chainman, block_index.pprev),
        .spend_options = block_index.pprev ? BuildCoreBlockSpendConsensusOptions(block_index, chainman) : Consensus::BlockSpendConsensusOptions{},
        .script_check_policy = {
            .assumed_valid_block = chainman.AssumedValidBlock(),
            .best_header = chainman.m_best_header,
            .minimum_chain_work = chainman.MinimumChainWork(),
        },
    };
}

CoreBlockConnectionPlan PlanCoreBlockConnection(const CoreBlockConnectionPolicySnapshot& policy, BlockIndexLookup& block_index, const CBlockIndex& block_index_entry)
{
    const bool has_spend_stage{block_index_entry.pprev != nullptr};
    CoreBlockScriptCheckDecision script_check_decision{.run_script_checks = true};
    if (has_spend_stage) {
        script_check_decision = DetermineCoreBlockScriptChecks(policy.script_check_policy, block_index, block_index_entry, policy.consensus_params);
    }

    return {
        .context = {
            .consensus_params = policy.consensus_params,
            .consensus_context = Consensus::BuildBlockConsensusContext(
                policy.header_context,
                block_index_entry.GetBlockHash(),
                Consensus::CalculateBlockSubsidy(block_index_entry.nHeight, policy.consensus_params)),
            .spend_options = has_spend_stage ? policy.spend_options : Consensus::BlockSpendConsensusOptions{},
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
