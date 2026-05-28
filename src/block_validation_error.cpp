// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <block_validation_error.h>

#include <consensus/block_check.h>
#include <consensus/block_commit.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_spend.h>
#include <validation_state.h>

#include <cassert>

BlockValidationResult ToBlockValidationResult(Consensus::BlockConsensusIssue issue)
{
    switch (issue) {
    case Consensus::BlockConsensusIssue::Consensus:
        return BlockValidationResult::BLOCK_CONSENSUS;
    case Consensus::BlockConsensusIssue::InvalidHeader:
        return BlockValidationResult::BLOCK_INVALID_HEADER;
    case Consensus::BlockConsensusIssue::Mutated:
        return BlockValidationResult::BLOCK_MUTATED;
    case Consensus::BlockConsensusIssue::TimeFuture:
        return BlockValidationResult::BLOCK_TIME_FUTURE;
    }
    assert(false);
    return BlockValidationResult::BLOCK_CONSENSUS;
}

bool ApplyBlockCheckError(BlockValidationState& state, const Consensus::BlockCheckError& error)
{
    return state.Invalid(ToBlockValidationResult(error.issue), error.reject_reason, error.debug_message);
}

bool ApplyBlockSpendError(BlockValidationState& state, const Consensus::BlockSpendError& error)
{
    return state.Invalid(ToBlockValidationResult(error.issue), error.reject_reason, error.debug_message);
}

bool ApplyBlockCommitError(BlockValidationState& state, const Consensus::BlockCommitError& error)
{
    return state.Error(error.reject_reason);
}

bool ApplyBlockConsensusStageError(BlockValidationState& state, const Consensus::BlockConsensusStageError& error)
{
    if (!error.issue) {
        return state.Error(error.reject_reason);
    }
    return state.Invalid(ToBlockValidationResult(*error.issue), error.reject_reason, error.debug_message);
}
