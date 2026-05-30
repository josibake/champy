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
class Chainstate;
class BlockDataReader;
class BlockUndoReader;

DisconnectResult DisconnectBlock(BlockUndoReader& undo_reader, const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
DisconnectResult DisconnectBlock(Chainstate& chainstate, const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool RollforwardBlock(BlockDataReader& block_reader, const CBlockIndex* pindex, CCoinsViewCache& inputs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool RollforwardBlock(Chainstate& chainstate, const CBlockIndex* pindex, CCoinsViewCache& inputs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool ReplayBlocks(Chainstate& chainstate);

#endif // BITCOIN_VALIDATION_BLOCK_REPLAY_H
