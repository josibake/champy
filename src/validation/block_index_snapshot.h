// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_INDEX_SNAPSHOT_H
#define BITCOIN_VALIDATION_BLOCK_INDEX_SNAPSHOT_H

#include <arith_uint256.h>
#include <uint256.h>

#include <cstdint>

struct ChainWorkBlockSnapshot {
    uint256 hash{};
    uint256 parent_hash{};
    int height{-1};
    arith_uint256 chain_work{};
};

struct AcceptedBlockHeaderSnapshot {
    ChainWorkBlockSnapshot block{};
    int64_t block_time{0};
};

#endif // BITCOIN_VALIDATION_BLOCK_INDEX_SNAPSHOT_H
