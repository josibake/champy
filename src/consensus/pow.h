// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_POW_H
#define BITCOIN_CONSENSUS_POW_H

#include <arith_uint256.h>
#include <consensus/params.h>
#include <uint256.h>

#include <cstdint>
#include <optional>

namespace Consensus {
struct Params;

struct DifficultyAdjustmentContext {
    int next_height{0};
    unsigned int last_bits{0};
    int64_t last_block_time{0};
    unsigned int first_period_bits{0};
    int64_t first_period_block_time{0};
    std::optional<unsigned int> last_non_min_difficulty_bits;
};

std::optional<arith_uint256> DeriveProofOfWorkTarget(unsigned int nBits, uint256 pow_limit);
bool CheckProofOfWorkHash(uint256 hash, unsigned int nBits, const Params& params);
bool CheckProofOfWorkHashImpl(uint256 hash, unsigned int nBits, const Params& params);

} // namespace Consensus

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, uint256 pow_limit);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits. */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);

namespace Consensus {

unsigned int CalculateNextWorkRequired(const DifficultyAdjustmentContext& context, const Params& params);
unsigned int GetNextWorkRequired(const DifficultyAdjustmentContext& context, int64_t candidate_block_time, const Params& params);
bool CheckPermittedDifficultyTransition(const Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_POW_H
