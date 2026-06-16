// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

namespace {

Consensus::DifficultyAdjustmentContext BuildDifficultyAdjustmentContext(const CBlockIndex& last_block, const Consensus::Params& params)
{
    const int difficulty_adjustment_interval{static_cast<int>(params.DifficultyAdjustmentInterval())};
    const int next_height{last_block.nHeight + 1};
    const unsigned int proof_of_work_limit{UintToArith256(params.powLimit).GetCompact()};

    Consensus::DifficultyAdjustmentContext context{
        .next_height = next_height,
        .last_bits = last_block.nBits,
        .last_block_time = last_block.GetBlockTime(),
        .first_period_bits = last_block.nBits,
        .first_period_block_time = last_block.GetBlockTime(),
        .last_non_min_difficulty_bits = last_block.nBits,
    };

    if (next_height % difficulty_adjustment_interval == 0) {
        const int first_height{last_block.nHeight - (difficulty_adjustment_interval - 1)};
        assert(first_height >= 0);
        const CBlockIndex* first_block{last_block.GetAncestor(first_height)};
        assert(first_block);
        context.first_period_bits = first_block->nBits;
        context.first_period_block_time = first_block->GetBlockTime();
    }

    if (params.fPowAllowMinDifficultyBlocks && next_height % difficulty_adjustment_interval != 0) {
        const CBlockIndex* cursor{&last_block};
        while (cursor->pprev && cursor->nHeight % difficulty_adjustment_interval != 0 && cursor->nBits == proof_of_work_limit) {
            cursor = cursor->pprev;
        }
        context.last_non_min_difficulty_bits = cursor->nBits;
    }

    return context;
}

} // namespace

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    const Consensus::DifficultyAdjustmentContext context{BuildDifficultyAdjustmentContext(*pindexLast, params)};
    const bool needs_candidate_time{
        params.fPowAllowMinDifficultyBlocks &&
        context.next_height % params.DifficultyAdjustmentInterval() != 0};
    assert(pblock != nullptr || !needs_candidate_time);
    const int64_t candidate_block_time{pblock ? pblock->GetBlockTime() : pindexLast->GetBlockTime()};
    return Consensus::GetNextWorkRequired(context, candidate_block_time, params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    Consensus::DifficultyAdjustmentContext context{
        .next_height = pindexLast->nHeight + 1,
        .last_bits = pindexLast->nBits,
        .last_block_time = pindexLast->GetBlockTime(),
        .first_period_bits = pindexLast->nBits,
        .first_period_block_time = nFirstBlockTime,
        .last_non_min_difficulty_bits = pindexLast->nBits,
    };

    if (params.enforce_BIP94) {
        const int first_height{pindexLast->nHeight - (static_cast<int>(params.DifficultyAdjustmentInterval()) - 1)};
        const CBlockIndex* first_block{pindexLast->GetAncestor(first_height)};
        assert(first_block);
        context.first_period_bits = first_block->nBits;
    }

    return Consensus::CalculateNextWorkRequired(context, params);
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    return Consensus::CheckPermittedDifficultyTransition(params, height, old_nbits, new_nbits);
}
