// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_ACTIVE_CHAIN_H
#define BITCOIN_VALIDATION_ACTIVE_CHAIN_H

#include <arith_uint256.h>
#include <kernel/cs_main.h>
#include <uint256.h>

#include <cstdint>

class CBlockIndex;

namespace validation {

struct ChainBlockSnapshot {
    uint256 hash{};
    int height{-1};
    int64_t time{0};
};

struct ActiveChainTipSnapshot {
    uint256 hash{};
    uint256 parent_hash{};
    int height{-1};
    int64_t time{0};
    arith_uint256 chain_work{};
    arith_uint256 block_proof{};
};

using BestHeaderSnapshot = ChainBlockSnapshot;

class ActiveChainView
{
public:
    virtual ~ActiveChainView() = default;

    [[nodiscard]] virtual CBlockIndex* Tip() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual int Height() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual CBlockIndex* Next(const CBlockIndex& block_index) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_ACTIVE_CHAIN_H
