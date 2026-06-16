// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_REPLAY_H
#define BITCOIN_VALIDATION_BLOCK_REPLAY_H

#include <coins.h>
#include <kernel/cs_main.h>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class BlockDataReader;
class BlockIndexLookup;
class BlockUndoReader;
class CCoinsView;

namespace kernel {
class Notifications;
} // namespace kernel

/**
 * Dependencies needed to repair an interrupted coins DB flush.
 *
 * Replay uses Core's current block-index entries, but it does not need a broad
 * Chainstate object. Keeping the storage, index, and notification capabilities
 * explicit makes replay testable and keeps the recovery algorithm reusable for
 * alternate coins/state backends.
 */
struct BlockReplayRequest {
    CCoinsView& coins_db;
    BlockDataReader& block_reader;
    BlockUndoReader& undo_reader;
    BlockIndexLookup& block_index;
    kernel::Notifications& notifications;
};

DisconnectResult DisconnectBlock(BlockUndoReader& undo_reader, const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool RollforwardBlock(BlockDataReader& block_reader, const CBlockIndex* pindex, CCoinsViewCache& inputs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool ReplayBlocks(const BlockReplayRequest& request) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_VALIDATION_BLOCK_REPLAY_H
