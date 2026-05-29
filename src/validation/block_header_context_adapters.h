// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_HEADER_CONTEXT_ADAPTERS_H
#define BITCOIN_BLOCK_HEADER_CONTEXT_ADAPTERS_H

#include <consensus/block_consensus_pipeline.h>
#include <kernel/cs_main.h>

class CBlockIndex;
class ChainstateManager;

[[nodiscard]] Consensus::BlockHeaderContext BuildCoreBlockHeaderContext(const ChainstateManager& chainman, const CBlockIndex* previous_index);

#endif // BITCOIN_BLOCK_HEADER_CONTEXT_ADAPTERS_H
