// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_policy.h>

#include <validation/block_index_adapters.h>
#include <validation/block_validation_policy.h>
#include <chain.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <pow.h>
#include <util/check.h>
#include <util/log.h>

#include <cassert>
#include <cstdint>

CoreBlockScriptCheckDecision DetermineCoreBlockScriptChecks(const CoreBlockScriptCheckPolicy& policy, BlockIndexLookup& block_index, const CBlockIndex& block_index_entry, const Consensus::Params& consensus_params)
{
    if (policy.assumed_valid_block.IsNull()) {
        return {.run_script_checks = true, .reason = "assumevalid=0 (always verify)"};
    }

    constexpr int64_t TWO_WEEKS_IN_SECONDS{60 * 60 * 24 * 7 * 2};
    // Assumevalid is Core validation policy, not consensus. It decides whether
    // this node executes script checks for ancestors of a configured block.
    const CBlockIndex* assumed_valid_index{block_index.LookupBlockIndex(policy.assumed_valid_block)};
    if (!assumed_valid_index) {
        return {.run_script_checks = true, .reason = "assumevalid hash not in headers"};
    }
    if (assumed_valid_index->GetAncestor(block_index_entry.nHeight) != &block_index_entry) {
        return {
            .run_script_checks = true,
            .reason = block_index_entry.nHeight > assumed_valid_index->nHeight ? "block height above assumevalid height" : "block not in assumevalid chain"};
    }
    const CBlockIndex& best_header{*Assert(policy.best_header)};
    if (best_header.GetAncestor(block_index_entry.nHeight) != &block_index_entry) {
        return {.run_script_checks = true, .reason = "block not in best header chain"};
    }
    if (best_header.nChainWork < policy.minimum_chain_work) {
        return {.run_script_checks = true, .reason = "best header chainwork below minimumchainwork"};
    }
    if (GetBlockProofEquivalentTime(best_header, block_index_entry, best_header, consensus_params) <= TWO_WEEKS_IN_SECONDS) {
        return {.run_script_checks = true, .reason = "block too recent relative to best header"};
    }

    // This block is an ancestor of the configured assumevalid block, is on the
    // best header chain, has enough chainwork, and is sufficiently buried. Core
    // may skip script execution; structural checks still run.
    return {};
}

void MaybeLogCoreBlockScriptCheckDecision(std::optional<const char*>& last_reason_logged, const CBlockIndex& block_index, const uint256& block_hash, const CoreBlockScriptCheckDecision& decision)
{
    if (decision.reason == last_reason_logged) return;

    if (decision.run_script_checks) {
        LogInfo("Enabling script verification at block #%d (%s): %s.",
                block_index.nHeight, block_hash.ToString(), decision.reason);
    } else {
        LogInfo("Disabling script verification at block #%d (%s).",
                block_index.nHeight, block_hash.ToString());
    }
    last_reason_logged = decision.reason;
}

namespace {

bool ShouldCheckBlockNoUnspentOutputOverwrite(const CBlockIndex& block_index, const Consensus::Params& consensus_params)
{
    // Do not allow blocks that contain transactions which overwrite older
    // transactions, unless those older outputs are already spent. See BIP30 and
    // CVE-2012-1909.
    bool enforce_bip30{!IsBIP30Repeat(block_index)};

    // Once BIP34 activated it was not possible to create new duplicate coinbases
    // on the known chain. The historical exceptions and the pre-BIP34 indicated
    // height issue require preserving Core's existing applicability rule.
    static constexpr int BIP34_IMPLIES_BIP30_LIMIT{1983702};
    assert(block_index.pprev);
    const CBlockIndex* bip34_index{block_index.pprev->GetAncestor(consensus_params.BIP34Height)};
    enforce_bip30 = enforce_bip30 && (!bip34_index || bip34_index->GetBlockHash() != consensus_params.BIP34Hash);

    // TODO: Remove BIP30 checking from block height 1,983,702 on, once a
    // consensus change ensures coinbases at those heights cannot duplicate
    // earlier coinbases.
    return enforce_bip30 || block_index.nHeight >= BIP34_IMPLIES_BIP30_LIMIT;
}

} // namespace

Consensus::BlockSpendConsensusOptions BuildCoreBlockSpendConsensusOptions(const CBlockIndex& block_index, const Consensus::Params& consensus_params, Consensus::BlockDeploymentContext deployments)
{
    int locktime_flags{0};
    if (deployments.csv_active) {
        locktime_flags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    return Consensus::BlockSpendConsensusOptions{
        .locktime_flags = locktime_flags,
        .script_flags = GetBlockScriptFlags(block_index, consensus_params, deployments),
        .check_no_unspent_output_overwrite = ShouldCheckBlockNoUnspentOutputOverwrite(block_index, consensus_params),
    };
}
