// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_BLOCK_DOWNLOAD_TYPES_H
#define BITCOIN_NODE_BLOCK_DOWNLOAD_TYPES_H

#include <uint256.h>
#include <validation/block_candidate_ref.h>

#include <cstdint>

namespace node {

using PeerBlockRef = validation::BlockCandidateRef;

struct BlockInFlight {
    uint256 hash{};
    int64_t peer{-1};
};

} // namespace node

#endif // BITCOIN_NODE_BLOCK_DOWNLOAD_TYPES_H
