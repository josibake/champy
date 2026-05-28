// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/pow.h>

#include <cassert>

namespace {

constexpr bool FuzzDeterministicProofOfWork()
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    return true;
#else
    return false;
#endif
}

} // namespace

namespace Consensus {

bool CheckProofOfWorkHash(uint256 hash, unsigned int nBits, const Params& params)
{
    if constexpr (FuzzDeterministicProofOfWork()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkHashImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveProofOfWorkTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit)) {
        return {};
    }

    return bnTarget;
}

bool CheckProofOfWorkHashImpl(uint256 hash, unsigned int nBits, const Params& params)
{
    auto bnTarget{DeriveProofOfWorkTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget) return false;

    return true;
}

} // namespace Consensus

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    return Consensus::CheckProofOfWorkHash(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    return Consensus::DeriveProofOfWorkTarget(nBits, pow_limit);
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    return Consensus::CheckProofOfWorkHashImpl(hash, nBits, params);
}

namespace Consensus {

unsigned int CalculateNextWorkRequired(const DifficultyAdjustmentContext& context, const Params& params)
{
    if (params.fPowNoRetargeting) return context.last_bits;

    int64_t actual_timespan{context.last_block_time - context.first_period_block_time};
    if (actual_timespan < params.nPowTargetTimespan / 4) actual_timespan = params.nPowTargetTimespan / 4;
    if (actual_timespan > params.nPowTargetTimespan * 4) actual_timespan = params.nPowTargetTimespan * 4;

    const arith_uint256 pow_limit{UintToArith256(params.powLimit)};
    arith_uint256 new_target;

    if (params.enforce_BIP94) {
        new_target.SetCompact(context.first_period_bits);
    } else {
        new_target.SetCompact(context.last_bits);
    }

    new_target *= actual_timespan;
    new_target /= params.nPowTargetTimespan;

    if (new_target > pow_limit) new_target = pow_limit;

    return new_target.GetCompact();
}

unsigned int GetNextWorkRequired(const DifficultyAdjustmentContext& context, int64_t candidate_block_time, const Params& params)
{
    assert(context.next_height > 0);
    const int64_t difficulty_adjustment_interval{params.DifficultyAdjustmentInterval()};
    assert(difficulty_adjustment_interval > 0);

    const unsigned int proof_of_work_limit{UintToArith256(params.powLimit).GetCompact()};

    if (context.next_height % difficulty_adjustment_interval != 0) {
        if (params.fPowAllowMinDifficultyBlocks) {
            if (candidate_block_time > context.last_block_time + params.nPowTargetSpacing * 2) {
                return proof_of_work_limit;
            }
            return context.last_non_min_difficulty_bits.value_or(context.last_bits);
        }
        return context.last_bits;
    }

    return CalculateNextWorkRequired(context, params);
}

bool CheckPermittedDifficultyTransition(const Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        const int64_t smallest_timespan{params.nPowTargetTimespan / 4};
        const int64_t largest_timespan{params.nPowTargetTimespan * 4};

        const arith_uint256 pow_limit{UintToArith256(params.powLimit)};
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

} // namespace Consensus
