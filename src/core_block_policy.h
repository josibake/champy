// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_BLOCK_POLICY_H
#define BITCOIN_CORE_BLOCK_POLICY_H

#include <consensus/block_spend.h>
#include <kernel/cs_main.h>

class CBlockIndex;
class Chainstate;
class ChainstateManager;
class uint256;

struct CoreBlockScriptCheckDecision {
    bool run_script_checks{false};
    const char* reason{nullptr};
};

[[nodiscard]] CoreBlockScriptCheckDecision DetermineCoreBlockScriptChecks(const Chainstate& chainstate, const CBlockIndex& block_index, const Consensus::Params& consensus_params)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

void MaybeLogCoreBlockScriptCheckDecision(Chainstate& chainstate, const CBlockIndex& block_index, const uint256& block_hash, const CoreBlockScriptCheckDecision& decision)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

[[nodiscard]] Consensus::BlockSpendConsensusOptions BuildCoreBlockSpendConsensusOptions(const CBlockIndex& block_index, const ChainstateManager& chainman)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_CORE_BLOCK_POLICY_H
