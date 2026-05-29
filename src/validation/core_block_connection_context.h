// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_CONTEXT_H
#define BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_CONTEXT_H

#include <kernel/cs_main.h>
#include <validation/block_connection.h>
#include <validation/core_block_policy.h>

#include <optional>

class CBlockIndex;
class ChainstateManager;
class BlockIndexLookup;
class uint256;

struct CoreBlockConnectionPolicySnapshot {
    const Consensus::Params& consensus_params;
    Consensus::BlockHeaderContext header_context;
    Consensus::BlockSpendConsensusOptions spend_options;
    CoreBlockScriptCheckPolicy script_check_policy;
};

struct CoreBlockConnectionPlan {
    validation::BlockConnectionContext context;
    CoreBlockScriptCheckDecision script_check_decision;
    bool has_spend_stage{false};
};

[[nodiscard]] CoreBlockConnectionPolicySnapshot SnapshotCoreBlockConnectionPolicy(ChainstateManager& chainman, const CBlockIndex& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

[[nodiscard]] CoreBlockConnectionPlan PlanCoreBlockConnection(const CoreBlockConnectionPolicySnapshot& policy, BlockIndexLookup& block_index, const CBlockIndex& block_index_entry)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

void MaybeLogCoreBlockConnectionScriptPolicy(std::optional<const char*>& last_reason_logged, const CBlockIndex& block_index, const uint256& block_hash, const CoreBlockConnectionPlan& plan)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_CONTEXT_H
