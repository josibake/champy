// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_replay.h>

#include <validation/block_coin_effects.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>

#include <chain.h>
#include <chainstate.h>
#include <coins.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <undo.h>
#include <util/check.h>
#include <util/log.h>
#include <util/translation.h>

#include <cassert>
#include <vector>

DisconnectResult DisconnectBlock(BlockUndoReader& undo_reader, const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view)
{
    AssertLockHeld(::cs_main);
    bool fClean = true;

    CBlockUndo blockUndo;
    if (!undo_reader.ReadBlockUndo(blockUndo, *pindex)) {
        LogError("DisconnectBlock(): failure reading undo data\n");
        return DISCONNECT_FAILED;
    }

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
    const bool fEnforceBIP30{!((pindex->nHeight == 91722 && pindex->GetBlockHash() == uint256{"00000000000271a2dc26e7667f8419f2e15416dc6955e5a6c6cdf3f2574dd08e"}) ||
                               (pindex->nHeight == 91812 && pindex->GetBlockHash() == uint256{"00000000000af0aed4792b1acee3d966af36cf5def14935db8de83d6f9306f2f"}))};

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
                if (!is_spent || tx.vout[o] != coin.out || pindex->nHeight != coin.nHeight || is_coinbase != coin.IsCoinBase()) {
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

    view.SetBestBlock(pindex->pprev->GetBlockHash());

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

DisconnectResult DisconnectBlock(Chainstate& chainstate, const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view)
{
    CoreBlockDataStore block_store{chainstate.m_blockman};
    return DisconnectBlock(block_store, block, pindex, view);
}

bool RollforwardBlock(BlockDataReader& block_reader, const CBlockIndex* pindex, CCoinsViewCache& inputs)
{
    AssertLockHeld(::cs_main);
    CBlock block;
    if (!block_reader.ReadBlock(block, *pindex)) {
        LogError("ReplayBlock(): ReadBlock failed at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        return false;
    }

    validation::ReplayBlockCoinsForRecovery(block, inputs, pindex->nHeight);
    return true;
}

bool RollforwardBlock(Chainstate& chainstate, const CBlockIndex* pindex, CCoinsViewCache& inputs)
{
    CoreBlockDataStore block_store{chainstate.m_blockman};
    return RollforwardBlock(block_store, pindex, inputs);
}

bool ReplayBlocks(Chainstate& chainstate)
{
    LOCK(::cs_main);

    CCoinsView& db = chainstate.CoinsDB();
    CCoinsViewCache cache(&db);
    CoreBlockDataStore block_store{chainstate.m_blockman};
    CoreBlockIndexStore block_index{chainstate.m_chainman};

    std::vector<uint256> hashHeads = db.GetHeadBlocks();
    if (hashHeads.empty()) return true;
    if (hashHeads.size() != 2) {
        LogError("ReplayBlocks(): unknown inconsistent state\n");
        return false;
    }

    chainstate.m_chainman.GetNotifications().progress(_("Replaying blocks…"), 0, false);
    LogInfo("Replaying blocks");

    const CBlockIndex* pindexOld = nullptr;
    const CBlockIndex* pindexNew{block_index.LookupBlockIndex(hashHeads[0])};
    const CBlockIndex* pindexFork = nullptr;
    if (!pindexNew) {
        LogError("ReplayBlocks(): reorganization to unknown block requested\n");
        return false;
    }

    if (!hashHeads[1].IsNull()) {
        pindexOld = block_index.LookupBlockIndex(hashHeads[1]);
        if (!pindexOld) {
            LogError("ReplayBlocks(): reorganization from unknown block requested\n");
            return false;
        }
        pindexFork = LastCommonAncestor(pindexOld, pindexNew);
        assert(pindexFork != nullptr);
    }

    const int fork_height{pindexFork ? pindexFork->nHeight : 0};
    if (pindexOld != pindexFork) {
        LogInfo("Rolling back from %s (%i to %i)", pindexOld->GetBlockHash().ToString(), pindexOld->nHeight, fork_height);
        while (pindexOld != pindexFork) {
            if (pindexOld->nHeight > 0) {
                CBlock block;
                if (!block_store.ReadBlock(block, *pindexOld)) {
                    LogError("RollbackBlock(): ReadBlock() failed at %d, hash=%s\n", pindexOld->nHeight, pindexOld->GetBlockHash().ToString());
                    return false;
                }
                if (pindexOld->nHeight % 10'000 == 0) {
                    LogInfo("Rolling back %s (%i)", pindexOld->GetBlockHash().ToString(), pindexOld->nHeight);
                }
                const DisconnectResult disconnect_result{DisconnectBlock(block_store, block, pindexOld, cache)};
                if (disconnect_result == DISCONNECT_FAILED) {
                    LogError("RollbackBlock(): DisconnectBlock failed at %d, hash=%s\n", pindexOld->nHeight, pindexOld->GetBlockHash().ToString());
                    return false;
                }
                // DISCONNECT_UNCLEAN is recoverable here: rollback is repairing
                // an interrupted flush, and coin writes/deletes are idempotent.
            }
            pindexOld = pindexOld->pprev;
        }
        LogInfo("Rolled back to %s", pindexFork->GetBlockHash().ToString());
    }

    if (fork_height < pindexNew->nHeight) {
        LogInfo("Rolling forward to %s (%i to %i)", pindexNew->GetBlockHash().ToString(), fork_height, pindexNew->nHeight);
        for (int height = fork_height + 1; height <= pindexNew->nHeight; ++height) {
            const CBlockIndex& pindex{*Assert(pindexNew->GetAncestor(height))};

            if (height % 10'000 == 0) {
                LogInfo("Rolling forward %s (%i)", pindex.GetBlockHash().ToString(), height);
            }
            chainstate.m_chainman.GetNotifications().progress(_("Replaying blocks…"), (int)((height - fork_height) * 100.0 / (pindexNew->nHeight - fork_height)), false);
            if (!RollforwardBlock(block_store, &pindex, cache)) return false;
        }
        LogInfo("Rolled forward to %s", pindexNew->GetBlockHash().ToString());
    }

    cache.SetBestBlock(pindexNew->GetBlockHash());
    cache.Flush(/*reallocate_cache=*/false);
    chainstate.m_chainman.GetNotifications().progress(bilingual_str{}, 100, false);
    return true;
}
