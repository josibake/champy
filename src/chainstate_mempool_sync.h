// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINSTATE_MEMPOOL_SYNC_H
#define BITCOIN_CHAINSTATE_MEMPOOL_SYNC_H

#include <chainstate_cache.h>
#include <kernel/cs_main.h>
#include <sync.h>

#include <cstdint>

class CBlock;
class CCoinsViewCache;
class Chainstate;

/**
 * Optional side-effect boundary for keeping node-owned mempool state consistent
 * with chainstate transitions.
 *
 * Callers that mutate mempool state must hold `cs_main` and the mutex returned
 * by `Mutex()` across the full chainstate transition. The dynamic mutex cannot
 * be expressed in the base-class thread-safety annotation, so implementations
 * assert the concrete lock contract.
 */
class ChainstateMempoolSync
{
public:
    virtual ~ChainstateMempoolSync() = default;

    virtual RecursiveMutex* Mutex() const = 0;
    virtual ExternalCacheUsage CacheUsage() const = 0;

    virtual void AddTransactionsUpdated() EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void Check(const CCoinsViewCache& coins, int64_t spend_height) const EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void UpdateForDisconnectedBlock(const CBlock& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void UpdateForConnectedBlock(const CBlock& block, unsigned int block_height) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual void UpdateForReorg(Chainstate& chainstate, bool add_to_mempool) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
};

#endif // BITCOIN_CHAINSTATE_MEMPOOL_SYNC_H
