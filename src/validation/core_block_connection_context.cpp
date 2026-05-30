// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_context.h>

#include <chain.h>
#include <consensus/amount.h>
#include <consensus/block_spend.h>
#include <consensus/params.h>
#include <validation/block_header_context_adapters.h>
#include <validation/coins_view_spend_state.h>
#include <validation/core_chain_validation_context.h>

#include <memory>

CoreBlockConnectionPolicySnapshot SnapshotCoreBlockConnectionPolicy(CoreChainValidationContext& context, const CBlockIndex& block_index)
{
    const Consensus::Params& consensus_params{context.ConsensusParams()};
    const CoreBlockHeaderContextProvider header_context{context.MakeHeaderContextProvider()};
    return {
        .consensus_params = consensus_params,
        .header_context = header_context.BuildContext(block_index.pprev),
        .script_check_policy = {
            .assumed_valid_block = context.AssumedValidBlock(),
            .best_header = context.BestHeader(),
            .minimum_chain_work = context.MinimumChainWork(),
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
            .sequence_lock_times = has_spend_stage ? std::make_shared<validation::CoinsViewSequenceLockTimeView>(block_index_entry) : nullptr,
            .spend_options = has_spend_stage ? BuildCoreBlockSpendConsensusOptions(block_index_entry, policy.consensus_params, policy.header_context.Deployments()) : Consensus::BlockSpendConsensusOptions{},
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
