// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_NODE_MEMPOOL_CHAIN_SYNC_H
#define BITCOIN_NODE_MEMPOOL_CHAIN_SYNC_H

#include <chainstate_mempool_sync.h>
#include <node/disconnected_transactions.h>
#include <node/txmempool.h>
#include <sync.h>

#include <cstddef>

class Chainstate;
class CBlock;
class CCoinsViewCache;

namespace node {

class MempoolChainSync final : public ChainstateMempoolSync
{
public:
    explicit MempoolChainSync(CTxMemPool& mempool) : m_mempool{mempool} {}

    RecursiveMutex* Mutex() const override LOCK_RETURNED(m_mempool.cs) { return &m_mempool.cs; }
    ExternalCacheUsage CacheUsage() const override
    {
        return {
            .max_size_bytes = m_mempool.m_opts.max_size_bytes,
            .usage_bytes = m_mempool.DynamicMemoryUsage(),
        };
    }

    void AddTransactionsUpdated() override EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void Check(const CCoinsViewCache& coins, int64_t spend_height) const override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Apply mempool bookkeeping for a block disconnected from the active chain.
     *
     * Current lock contract: callers hold `cs_main` and `mempool.cs` across
     * block disconnect and the later reorg repair step, so observers never see
     * a half-repaired mempool after the locks are released.
     */
    void UpdateForDisconnectedBlock(
        const CBlock& block) override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Remove transactions confirmed by a connected block from mempool state.
     */
    void UpdateForConnectedBlock(
        const CBlock& block,
        unsigned int block_height) override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Make mempool consistent after a reorg by re-adding or recursively erasing
     * transactions from disconnected blocks, then dropping entries that are no
     * longer final, mature, or within mempool size limits.
     */
    void UpdateForReorg(
        Chainstate& chainstate,
        bool add_to_mempool) override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    CTxMemPool& m_mempool;
    DisconnectedBlockTransactions m_disconnectpool{MAX_DISCONNECTED_TX_POOL_BYTES};
};

} // namespace node

#endif // BITCOIN_NODE_MEMPOOL_CHAIN_SYNC_H
