// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_NODE_MEMPOOL_CHAIN_SYNC_H
#define BITCOIN_NODE_MEMPOOL_CHAIN_SYNC_H

#include <chainstate_event_sink.h>
#include <node/disconnected_transactions.h>
#include <node/txmempool.h>
#include <sync.h>

#include <cstddef>

class Chainstate;
class CBlock;

namespace node {

class MempoolChainSync final : public ChainstateEventSink
{
public:
    MempoolChainSync(Chainstate& chainstate, CTxMemPool& mempool) : m_chainstate{chainstate}, m_mempool{mempool} {}

    RecursiveMutex* Mutex() const override LOCK_RETURNED(m_mempool.cs) { return &m_mempool.cs; }
    ExternalCacheUsage CacheUsage() const override
    {
        return {
            .max_size_bytes = m_mempool.m_opts.max_size_bytes,
            .usage_bytes = m_mempool.DynamicMemoryUsage(),
        };
    }

    void TransactionsUpdated() override EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void CheckPostReorgState(int64_t spend_height) const override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Apply mempool bookkeeping for a block disconnected from the active chain.
     *
     * Current lock contract: callers hold `cs_main` and `mempool.cs` across
     * block disconnect and the later reorg repair step, so observers never see
     * a half-repaired mempool after the locks are released.
     */
    void BlockDisconnected(
        const CBlock& block) override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Remove transactions confirmed by a connected block from mempool state.
     */
    void BlockConnected(
        const CBlock& block,
        unsigned int block_height) override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Make mempool consistent after a reorg by re-adding or recursively erasing
     * transactions from disconnected blocks, then dropping entries that are no
     * longer final, mature, or within mempool size limits.
     */
    void ReorgCompleted(
        bool restore_disconnected_transactions) override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    Chainstate& m_chainstate;
    CTxMemPool& m_mempool;
    DisconnectedBlockTransactions m_disconnectpool{MAX_DISCONNECTED_TX_POOL_BYTES};
};

} // namespace node

#endif // BITCOIN_NODE_MEMPOOL_CHAIN_SYNC_H
