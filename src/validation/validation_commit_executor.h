// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_COMMIT_EXECUTOR_H
#define BITCOIN_VALIDATION_COMMIT_EXECUTOR_H

#include <kernel/cs_main.h>
#include <sync.h>
#include <validation/core_chain_lock.h>

#include <functional>
#include <utility>

namespace validation {

/**
 * Core's current implementation of serialized chainstate work.
 *
 * This intentionally keeps the existing locks: the chainstate mutex serializes
 * activation/invalidation callers, and cs_main protects active-chain,
 * block-index, and commit-visible state. The role-named entry points make the
 * runtime contract explicit so future implementations can split or replace it
 * without changing consensus-facing code.
 */
class CoreValidationCommitExecutor final
{
public:
    explicit CoreValidationCommitExecutor(Mutex& chainstate_mutex) : m_chainstate_mutex{chainstate_mutex} {}

    template <typename Fn>
    decltype(auto) RunSerialized(Fn&& fn) EXCLUSIVE_LOCKS_REQUIRED(!m_chainstate_mutex) LOCKS_EXCLUDED(::cs_main)
    {
        LOCK(m_chainstate_mutex);
        return std::invoke(std::forward<Fn>(fn));
    }

    template <typename Fn>
    decltype(auto) RunActiveChainLocked(Fn&& fn) LOCKS_EXCLUDED(::cs_main)
    {
        return RunCsMainLocked(std::forward<Fn>(fn));
    }

    template <typename Fn>
    decltype(auto) RunBlockIndexLocked(Fn&& fn) LOCKS_EXCLUDED(::cs_main)
    {
        return RunCsMainLocked(std::forward<Fn>(fn));
    }

    template <typename Fn>
    decltype(auto) RunChainstateCommitLocked(Fn&& fn) LOCKS_EXCLUDED(::cs_main)
    {
        return RunCsMainLocked(std::forward<Fn>(fn));
    }

    template <typename Fn>
    decltype(auto) RunStorageCoordinationLocked(Fn&& fn) LOCKS_EXCLUDED(::cs_main)
    {
        return RunCsMainLocked(std::forward<Fn>(fn));
    }

    template <typename Fn>
    decltype(auto) RunChainstateCommitLockedWithUnlock(Fn&& fn) LOCKS_EXCLUDED(::cs_main)
    {
        return RunCsMainLockedWithUnlock(std::forward<Fn>(fn));
    }

private:
    template <typename Fn>
    decltype(auto) RunCsMainLocked(Fn&& fn) LOCKS_EXCLUDED(::cs_main)
    {
        LOCK(::cs_main);
        return std::invoke(std::forward<Fn>(fn));
    }

    template <typename Fn>
    decltype(auto) RunCsMainLockedWithUnlock(Fn&& fn) LOCKS_EXCLUDED(::cs_main)
    {
        WAIT_LOCK(::cs_main, chain_lock_handle);
        CoreChainLock chain_lock{chain_lock_handle};
        return std::invoke(std::forward<Fn>(fn), chain_lock);
    }

    Mutex& m_chainstate_mutex;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_COMMIT_EXECUTOR_H
