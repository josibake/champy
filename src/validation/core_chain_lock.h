// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_CHAIN_LOCK_H
#define BITCOIN_VALIDATION_CORE_CHAIN_LOCK_H

#include <kernel/cs_main.h>
#include <sync.h>

#include <functional>
#include <utility>

/**
 * Explicit capability for temporarily releasing Core's active-chain lock.
 *
 * This keeps lock release sites reviewable and preserves Core's lock-order
 * instrumentation through REVERSE_LOCK. Callers must only use this when
 * `cs_main` is the most recently acquired lock.
 */
class CoreChainLock final
{
public:
    explicit CoreChainLock(UniqueLock<RecursiveMutex>& lock) : m_lock{lock} {}

    template <typename Fn>
    // Thread-safety analysis cannot prove that the held UniqueLock reference
    // corresponds to cs_main at indirect call sites. REVERSE_LOCK still checks
    // the runtime lock stack in debug builds.
    decltype(auto) RunUnlocked(Fn&& fn) NO_THREAD_SAFETY_ANALYSIS
    {
        REVERSE_LOCK(m_lock, ::cs_main);
        return std::invoke(std::forward<Fn>(fn));
    }

private:
    UniqueLock<RecursiveMutex>& m_lock;
};

#endif // BITCOIN_VALIDATION_CORE_CHAIN_LOCK_H
