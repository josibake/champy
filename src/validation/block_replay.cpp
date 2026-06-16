// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_replay.h>

#include <chain.h>
#include <coins.h>
#include <kernel/notifications_interface.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <undo.h>
#include <util/check.h>
#include <util/log.h>
#include <util/translation.h>
#include <validation/block_coin_effects.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index.h>
#include <validation/block_storage.h>

#include <cassert>
#include <utility>
#include <vector>

BlockReplayBlock SnapshotCoreBlockReplayBlock(const CBlockIndex& block_index)
{
    AssertLockHeld(::cs_main);
    return {
        .hash = block_index.GetBlockHash(),
        .previous_hash = block_index.pprev ? block_index.pprev->GetBlockHash() : uint256{},
        .height = block_index.nHeight,
        .block_read = SnapshotBlockDataReadRequest(block_index),
        .undo_read = block_index.pprev ? std::optional<BlockUndoReadRequest>{SnapshotBlockUndoReadRequest(block_index)} : std::nullopt,
    };
}

namespace {

bool SameReplayBlock(const std::optional<BlockReplayBlock>& a, const std::optional<BlockReplayBlock>& b)
{
    if (!a || !b) return !a && !b;
    return a->hash == b->hash;
}

} // namespace

CoreBlockReplayIndex::CoreBlockReplayIndex(BlockIndexLookup& block_index)
    : m_block_index{block_index}
{
}

std::optional<BlockReplayBlock> CoreBlockReplayIndex::LookupBlock(const uint256& block_hash) const
{
    if (const CBlockIndex* block_index{m_block_index.LookupBlockIndex(block_hash)}) {
        return SnapshotCoreBlockReplayBlock(*block_index);
    }
    return std::nullopt;
}

std::optional<BlockReplayBlock> CoreBlockReplayIndex::Previous(const BlockReplayBlock& block) const
{
    const CBlockIndex* block_index{m_block_index.LookupBlockIndex(block.hash)};
    if (!block_index || !block_index->pprev) return std::nullopt;
    return SnapshotCoreBlockReplayBlock(*block_index->pprev);
}

std::optional<BlockReplayBlock> CoreBlockReplayIndex::AncestorAtHeight(const BlockReplayBlock& block, int height) const
{
    const CBlockIndex* block_index{m_block_index.LookupBlockIndex(block.hash)};
    if (!block_index) return std::nullopt;
    if (const CBlockIndex* ancestor{block_index->GetAncestor(height)}) {
        return SnapshotCoreBlockReplayBlock(*ancestor);
    }
    return std::nullopt;
}

std::optional<BlockReplayBlock> CoreBlockReplayIndex::LastCommonAncestor(const BlockReplayBlock& a, const BlockReplayBlock& b) const
{
    const CBlockIndex* a_index{m_block_index.LookupBlockIndex(a.hash)};
    const CBlockIndex* b_index{m_block_index.LookupBlockIndex(b.hash)};
    if (!a_index || !b_index) return std::nullopt;
    if (const CBlockIndex* ancestor{::LastCommonAncestor(a_index, b_index)}) {
        return SnapshotCoreBlockReplayBlock(*ancestor);
    }
    return std::nullopt;
}

CoreBlockReplayCoins::CoreBlockReplayCoins(CCoinsView& coins_db)
    : m_cache{&coins_db}
{
}

std::vector<uint256> CoreBlockReplayCoins::GetHeadBlocks() const
{
    return m_cache.GetHeadBlocks();
}

DisconnectResult CoreBlockReplayCoins::DisconnectBlock(
    BlockUndoReader& undo_reader,
    const CBlock& block,
    const BlockReplayBlock& block_index) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return ::DisconnectBlock(undo_reader, block, block_index, m_cache);
}

bool CoreBlockReplayCoins::RollforwardBlock(const CBlock& block, const BlockReplayBlock& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return ::RollforwardBlock(block, block_index, m_cache);
}

void CoreBlockReplayCoins::SetBestBlock(const uint256& block_hash)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    m_cache.SetBestBlock(block_hash);
}

void CoreBlockReplayCoins::Flush()
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    m_cache.Flush(/*reallocate_cache=*/false);
}

DisconnectResult DisconnectBlock(BlockUndoReader& undo_reader, const CBlock& block, const BlockReplayBlock& block_index, CCoinsViewCache& view)
{
    AssertLockHeld(::cs_main);
    bool fClean = true;

    if (!block_index.undo_read) {
        LogError("DisconnectBlock(): failure reading undo data\n");
        return DISCONNECT_FAILED;
    }
    auto undo_result{undo_reader.ReadBlockUndo(*block_index.undo_read)};
    if (!undo_result) {
        LogError("DisconnectBlock(): failure reading undo data\n");
        return DISCONNECT_FAILED;
    }
    CBlockUndo blockUndo{std::move(*undo_result)};

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size()) {
        LogError("DisconnectBlock(): block and undo data inconsistent\n");
        return DISCONNECT_FAILED;
    }

    // Ignore blocks that contain transactions which are 'overwritten' by later transactions,
    // unless those are already completely spent.
    // See https://github.com/bitcoin/bitcoin/issues/22596 for additional information.
    // Note: the blocks specified here are different than the ones used in block connection because DisconnectBlock
    // unwinds the blocks in reverse. As a result, the inconsistency is not discovered until the earlier
    // blocks with the duplicate coinbase transactions are disconnected.
    const bool fEnforceBIP30{!((block_index.height == 91722 && block_index.hash == uint256{"00000000000271a2dc26e7667f8419f2e15416dc6955e5a6c6cdf3f2574dd08e"}) ||
                               (block_index.height == 91812 && block_index.hash == uint256{"00000000000af0aed4792b1acee3d966af36cf5def14935db8de83d6f9306f2f"}))};

    for (size_t tx_index = block.vtx.size(); tx_index > 0;) {
        --tx_index;
        const CTransaction& tx{*block.vtx[tx_index]};
        const Txid hash{tx.GetHash()};
        const bool is_coinbase{tx.IsCoinBase()};
        const bool is_bip30_exception{is_coinbase && !fEnforceBIP30};

        for (size_t o = 0; o < tx.vout.size(); o++) {
            if (!tx.vout[o].scriptPubKey.IsUnspendable()) {
                COutPoint out(hash, o);
                Coin coin;
                const bool is_spent{view.SpendCoin(out, &coin)};
                if (!is_spent || tx.vout[o] != coin.out || block_index.height != coin.nHeight || is_coinbase != coin.IsCoinBase()) {
                    if (!is_bip30_exception) {
                        fClean = false;
                    }
                }
            }
        }

        if (tx_index > 0) {
            CTxUndo& txundo{blockUndo.vtxundo[tx_index - 1]};
            if (txundo.vprevout.size() != tx.vin.size()) {
                LogError("DisconnectBlock(): transaction and undo data inconsistent\n");
                return DISCONNECT_FAILED;
            }
            for (unsigned int j = tx.vin.size(); j > 0;) {
                --j;
                const COutPoint& out{tx.vin[j].prevout};
                const int res{ApplyTxInUndo(std::move(txundo.vprevout[j]), view, out)};
                if (res == DISCONNECT_FAILED) return DISCONNECT_FAILED;
                fClean = fClean && res != DISCONNECT_UNCLEAN;
            }
        }
    }

    view.SetBestBlock(block_index.previous_hash);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

bool RollforwardBlock(const CBlock& block, const BlockReplayBlock& block_index, CCoinsViewCache& inputs)
{
    AssertLockHeld(::cs_main);
    validation::ReplayBlockCoinsForRecovery(block, inputs, block_index.height);
    return true;
}

bool ReplayBlocks(const BlockReplayRequest& request)
{
    AssertLockHeld(::cs_main);

    std::vector<uint256> hashHeads = request.coins.GetHeadBlocks();
    if (hashHeads.empty()) return true;
    if (hashHeads.size() != 2) {
        LogError("ReplayBlocks(): unknown inconsistent state\n");
        return false;
    }

    request.notifications.progress(_("Replaying blocks…"), 0, false);
    LogInfo("Replaying blocks");

    std::optional<BlockReplayBlock> pindexOld;
    const std::optional<BlockReplayBlock> pindexNew{request.block_index.LookupBlock(hashHeads[0])};
    std::optional<BlockReplayBlock> pindexFork;
    if (!pindexNew) {
        LogError("ReplayBlocks(): reorganization to unknown block requested\n");
        return false;
    }

    if (!hashHeads[1].IsNull()) {
        pindexOld = request.block_index.LookupBlock(hashHeads[1]);
        if (!pindexOld) {
            LogError("ReplayBlocks(): reorganization from unknown block requested\n");
            return false;
        }
        pindexFork = request.block_index.LastCommonAncestor(*pindexOld, *pindexNew);
        assert(pindexFork);
    }

    const int fork_height{pindexFork ? pindexFork->height : 0};
    if (!SameReplayBlock(pindexOld, pindexFork)) {
        LogInfo("Rolling back from %s (%i to %i)", pindexOld->hash.ToString(), pindexOld->height, fork_height);
        while (!SameReplayBlock(pindexOld, pindexFork)) {
            if (pindexOld->height > 0) {
                auto block_result{request.block_reader.ReadBlock(pindexOld->block_read)};
                if (!block_result) {
                    LogError("RollbackBlock(): ReadBlock() failed at %d, hash=%s\n", pindexOld->height, pindexOld->hash.ToString());
                    return false;
                }
                CBlock block{std::move(*block_result)};
                if (pindexOld->height % 10'000 == 0) {
                    LogInfo("Rolling back %s (%i)", pindexOld->hash.ToString(), pindexOld->height);
                }
                const DisconnectResult disconnect_result{request.coins.DisconnectBlock(request.undo_reader, block, *pindexOld)};
                if (disconnect_result == DISCONNECT_FAILED) {
                    LogError("RollbackBlock(): DisconnectBlock failed at %d, hash=%s\n", pindexOld->height, pindexOld->hash.ToString());
                    return false;
                }
                // DISCONNECT_UNCLEAN is recoverable here: rollback is repairing
                // an interrupted flush, and coin writes/deletes are idempotent.
            }
            pindexOld = request.block_index.Previous(*pindexOld);
            if (!pindexOld && pindexFork) {
                LogError("RollbackBlock(): previous block missing while rolling back to %s\n", pindexFork->hash.ToString());
                return false;
            }
        }
        LogInfo("Rolled back to %s", pindexFork ? pindexFork->hash.ToString() : uint256{}.ToString());
    }

    if (fork_height < pindexNew->height) {
        LogInfo("Rolling forward to %s (%i to %i)", pindexNew->hash.ToString(), fork_height, pindexNew->height);
        for (int height = fork_height + 1; height <= pindexNew->height; ++height) {
            const std::optional<BlockReplayBlock> pindex{request.block_index.AncestorAtHeight(*pindexNew, height)};
            if (!pindex) {
                LogError("ReplayBlock(): missing ancestor at height %d for hash=%s\n", height, pindexNew->hash.ToString());
                return false;
            }

            if (height % 10'000 == 0) {
                LogInfo("Rolling forward %s (%i)", pindex->hash.ToString(), height);
            }
            request.notifications.progress(_("Replaying blocks…"), (int)((height - fork_height) * 100.0 / (pindexNew->height - fork_height)), false);
            auto block_result{request.block_reader.ReadBlock(pindex->block_read)};
            if (!block_result) {
                LogError("ReplayBlock(): ReadBlock failed at %d, hash=%s\n", pindex->height, pindex->hash.ToString());
                return false;
            }
            CBlock block{std::move(*block_result)};
            if (!request.coins.RollforwardBlock(block, *pindex)) return false;
        }
        LogInfo("Rolled forward to %s", pindexNew->hash.ToString());
    }

    request.coins.SetBestBlock(pindexNew->hash);
    request.coins.Flush();
    request.notifications.progress(bilingual_str{}, 100, false);
    return true;
}
