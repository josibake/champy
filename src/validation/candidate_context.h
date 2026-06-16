// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CANDIDATE_CONTEXT_H
#define BITCOIN_VALIDATION_CANDIDATE_CONTEXT_H

#include <consensus/amount.h>
#include <consensus/block_check.h>
#include <consensus/block_spend.h>
#include <primitives/block.h>
#include <validation/block_index_snapshot.h>

#include <cstdint>

namespace validation {

struct AcceptedBlockContextSnapshot {
    ChainWorkBlockSnapshot block{};
    uint256 previous_block_hash{};
    int previous_block_height{-1};
    int64_t previous_median_time_past{0};
    int64_t previous_block_time{0};
    Consensus::BlockDeploymentContext deployments{};
    Consensus::BlockSpendConsensusOptions spend_options{};
    CAmount block_subsidy{0};
    bool has_spend_stage{false};

    [[nodiscard]] bool Matches(const CBlock& candidate) const { return block.hash == candidate.GetHash(); }
};

} // namespace validation

#endif // BITCOIN_VALIDATION_CANDIDATE_CONTEXT_H
