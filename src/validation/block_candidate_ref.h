// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_CANDIDATE_REF_H
#define BITCOIN_VALIDATION_BLOCK_CANDIDATE_REF_H

#include <arith_uint256.h>
#include <uint256.h>

namespace validation {

struct BlockCandidateRef {
    uint256 hash{};
    uint256 parent_hash{};
    int height{-1};
    arith_uint256 chain_work{};
};

} // namespace validation

#endif // BITCOIN_VALIDATION_BLOCK_CANDIDATE_REF_H
