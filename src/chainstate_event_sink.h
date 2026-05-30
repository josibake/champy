// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINSTATE_EVENT_SINK_H
#define BITCOIN_CHAINSTATE_EVENT_SINK_H

#include <chainstate_cache.h>
#include <kernel/cs_main.h>
#include <sync.h>

#include <cstdint>

class CBlock;

/**
 * Optional side-effect boundary for node-owned state that tracks chainstate
 * transitions.
 *
 * Compatibility note: Core's current mempool repair path requires callers to
 * hold `cs_main` and the mutex returned by `Mutex()` across a full chainstate
 * transition. Keep new event sinks narrow and document any lock they require;
 * do not build new architecture around this dynamic mutex shape.
 */
class ChainstateEventSink
{
public:
    virtual ~ChainstateEventSink() = default;

    virtual RecursiveMutex* Mutex() const = 0;
    virtual ExternalCacheUsage CacheUsage() const = 0;

    virtual void TransactionsUpdated() EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void CheckPostReorgState(int64_t spend_height) const EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void BlockDisconnected(const CBlock& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void BlockConnected(const CBlock& block, unsigned int block_height) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void ReorgCompleted(bool restore_disconnected_transactions) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
};

#endif // BITCOIN_CHAINSTATE_EVENT_SINK_H
