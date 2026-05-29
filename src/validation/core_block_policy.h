// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_BLOCK_POLICY_H
#define BITCOIN_CORE_BLOCK_POLICY_H

#include <arith_uint256.h>
#include <consensus/block_spend.h>
#include <kernel/cs_main.h>
#include <uint256.h>

#include <optional>

class CBlockIndex;
class ChainstateManager;
class BlockIndexLookup;
class uint256;

struct CoreBlockScriptCheckDecision {
    bool run_script_checks{false};
    const char* reason{nullptr};
};

struct CoreBlockScriptCheckPolicy {
    uint256 assumed_valid_block;
    const CBlockIndex* best_header{nullptr};
    arith_uint256 minimum_chain_work;
};

[[nodiscard]] CoreBlockScriptCheckDecision DetermineCoreBlockScriptChecks(const CoreBlockScriptCheckPolicy& policy, BlockIndexLookup& block_index, const CBlockIndex& block_index_entry, const Consensus::Params& consensus_params)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

void MaybeLogCoreBlockScriptCheckDecision(std::optional<const char*>& last_reason_logged, const CBlockIndex& block_index, const uint256& block_hash, const CoreBlockScriptCheckDecision& decision)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

[[nodiscard]] Consensus::BlockSpendConsensusOptions BuildCoreBlockSpendConsensusOptions(const CBlockIndex& block_index, const ChainstateManager& chainman)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_CORE_BLOCK_POLICY_H
