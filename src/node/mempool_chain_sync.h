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

    ExternalCacheUsage CacheUsage() const override
    {
        return {
            .max_size_bytes = m_mempool.m_opts.max_size_bytes,
            .usage_bytes = m_mempool.DynamicMemoryUsage(),
        };
    }

    void ProcessEvents(const ChainstateEventBatch& events) override EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    void TransactionsUpdated() EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool.cs);
    void CheckPostReorgState(int64_t spend_height) const EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool.cs);
    void BlockDisconnected(const CBlock& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool.cs);
    void BlockConnected(const CBlock& block, unsigned int block_height) EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool.cs);
    void ReorgCompleted(bool restore_disconnected_transactions) EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool.cs);

    Chainstate& m_chainstate;
    CTxMemPool& m_mempool;
    DisconnectedBlockTransactions m_disconnectpool{MAX_DISCONNECTED_TX_POOL_BYTES};
};

} // namespace node

#endif // BITCOIN_NODE_MEMPOOL_CHAIN_SYNC_H
