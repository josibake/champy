// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <node/mempool_chain_sync.h>

#include <chain.h>
#include <chainstate.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <node/mempool_validation.h>
#include <node/txmempool.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <validation/tx_verify.h>
#include <util/check.h>
#include <util/time.h>

#include <cassert>
#include <optional>
#include <vector>

namespace node {

void MempoolChainSync::TransactionsUpdated()
{
    AssertLockHeld(cs_main);
    m_mempool.AddTransactionsUpdated(1);
}

void MempoolChainSync::CheckPostReorgState(const CCoinsViewCache& coins, int64_t spend_height) const
{
    AssertLockHeld(cs_main);
    m_mempool.check(coins, spend_height);
}

void MempoolChainSync::BlockDisconnected(
    const CBlock& block) NO_THREAD_SAFETY_ANALYSIS
{
    AssertLockHeld(cs_main);
    AssertLockHeld(m_mempool.cs);

    // Save transactions to re-add to mempool at end of reorg. If any entries are evicted for
    // exceeding memory limits, remove them and their descendants from the mempool.
    for (auto&& evicted_tx : m_disconnectpool.AddTransactionsFromBlock(block.vtx)) {
        m_mempool.removeRecursive(*evicted_tx, MemPoolRemovalReason::REORG);
    }
}

void MempoolChainSync::BlockConnected(
    const CBlock& block,
    unsigned int block_height) NO_THREAD_SAFETY_ANALYSIS
{
    AssertLockHeld(cs_main);
    AssertLockHeld(m_mempool.cs);

    m_mempool.removeForBlock(block.vtx, block_height);
    m_disconnectpool.removeForBlock(block.vtx);
}

void MempoolChainSync::ReorgCompleted(
    Chainstate& chainstate,
    bool restore_disconnected_transactions) NO_THREAD_SAFETY_ANALYSIS
{
    AssertLockHeld(cs_main);
    AssertLockHeld(m_mempool.cs);

    std::vector<Txid> vHashUpdate;
    {
        // disconnectpool is ordered so that the front is the most recently-confirmed
        // transaction (the last tx of the block at the tip) in the disconnected chain.
        // Iterate disconnectpool in reverse, so that we add transactions
        // back to the mempool starting with the earliest transaction that had
        // been previously seen in a block.
        const auto queuedTx = m_disconnectpool.take();
        auto it = queuedTx.rbegin();
        while (it != queuedTx.rend()) {
            // ignore validation errors in resurrected transactions
            if (!restore_disconnected_transactions || (*it)->IsCoinBase() ||
                AcceptToMemoryPool(chainstate, m_mempool, *it, GetTime(),
                                   /*bypass_limits=*/true, /*test_accept=*/false)
                        .m_result_type !=
                    MempoolAcceptResult::ResultType::VALID) {
                // If the transaction doesn't make it in to the mempool, remove any
                // transactions that depend on it (which would now be orphans).
                m_mempool.removeRecursive(**it, MemPoolRemovalReason::REORG);
            } else if (m_mempool.exists((*it)->GetHash())) {
                vHashUpdate.push_back((*it)->GetHash());
            }
            ++it;
        }
    }

    // AcceptToMemoryPool/addNewTransaction all assume that new mempool entries have
    // no in-mempool children, which is generally not true when adding
    // previously-confirmed transactions back to the mempool.
    // UpdateTransactionsFromBlock finds descendants of any transactions in
    // the disconnectpool that were added back and cleans up the mempool state.
    m_mempool.UpdateTransactionsFromBlock(vHashUpdate);

    // Predicate to use for filtering transactions in removeForReorg.
    // Checks whether the transaction is still final and, if it spends a coinbase output, mature.
    // Also updates valid entries' cached LockPoints if needed.
    // If false, the tx is still valid and its lockpoints are updated.
    // If true, the tx would be invalid in the next block; remove this entry and all of its descendants.
    // Note that TRUC rules are not applied here, so reorgs may cause violations of TRUC inheritance or
    // topology restrictions.
    const auto filter_final_and_mature = [&](CTxMemPool::txiter it)
                                             EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs, ::cs_main) {
                                                 AssertLockHeld(m_mempool.cs);
                                                 AssertLockHeld(::cs_main);
                                                 const CTransaction& tx = it->GetTx();

                                                 // The transaction must be final.
                                                 if (!CheckFinalTxAtTip(*Assert(chainstate.m_chain.Tip()), tx)) return true;

                                                 const LockPoints& lp = it->GetLockPoints();
                                                 // CheckSequenceLocksAtTip checks if the transaction will be final in the next block to be
                                                 // created on top of the new chain.
                                                 if (TestLockPointValidity(chainstate.m_chain, lp)) {
                                                     if (!CheckSequenceLocksAtTip(chainstate.m_chain.Tip(), lp)) {
                                                         return true;
                                                     }
                                                 } else {
                                                     const CCoinsViewMemPool view_mempool{&chainstate.CoinsTip(), m_mempool};
                                                     const std::optional<LockPoints> new_lock_points{CalculateLockPointsAtTip(chainstate.m_chain.Tip(), view_mempool, tx)};
                                                     if (new_lock_points.has_value() && CheckSequenceLocksAtTip(chainstate.m_chain.Tip(), *new_lock_points)) {
                                                         // Now update the mempool entry lockpoints as well.
                                                         it->UpdateLockPoints(*new_lock_points);
                                                     } else {
                                                         return true;
                                                     }
                                                 }

                                                 // If the transaction spends any coinbase outputs, it must be mature.
                                                 if (it->GetSpendsCoinbase()) {
                                                     for (const CTxIn& txin : tx.vin) {
                                                         if (m_mempool.exists(txin.prevout.hash)) continue;
                                                         const Coin& coin{chainstate.CoinsTip().AccessCoin(txin.prevout)};
                                                         assert(!coin.IsSpent());
                                                         const auto mempool_spend_height{chainstate.m_chain.Tip()->nHeight + 1};
                                                         if (coin.IsCoinBase() && mempool_spend_height - coin.nHeight < COINBASE_MATURITY) {
                                                             return true;
                                                         }
                                                     }
                                                 }
                                                 // Transaction is still valid and cached LockPoints are updated.
                                                 return false;
                                             };

    // We also need to remove any now-immature transactions
    m_mempool.removeForReorg(chainstate.m_chain, filter_final_and_mature);
    // Re-limit mempool size, in case we added any transactions
    LimitMempoolSize(m_mempool, chainstate.CoinsTip());
}

} // namespace node
