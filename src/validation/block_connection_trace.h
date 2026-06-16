// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_CONNECTION_TRACE_H
#define BITCOIN_VALIDATION_BLOCK_CONNECTION_TRACE_H

#include <kernel/cs_main.h>
#include <util/time.h>

#include <chrono>
#include <cstddef>
#include <cstdint>

class ChainstateManager;

struct BlockConnectionStageTimings {
    std::chrono::nanoseconds sanity_checks{0};
    std::chrono::nanoseconds fork_checks{0};
    std::chrono::nanoseconds spend_join{0};
    std::chrono::nanoseconds script_validation{0};
    std::chrono::nanoseconds undo_write{0};
    std::chrono::nanoseconds index_commit{0};
    std::chrono::nanoseconds total{0};
};

struct BlockConnectionTraceCounters {
    // Optional Core global counters. The default trace records local stage
    // timings only; traces with these counters must be advanced under the
    // same lock that protects the referenced ChainstateManager timings.
    int64_t* num_blocks_total{nullptr};
    SteadyClock::duration* time_check{nullptr};
    SteadyClock::duration* time_forks{nullptr};
    SteadyClock::duration* time_connect{nullptr};
    SteadyClock::duration* time_verify{nullptr};
    SteadyClock::duration* time_undo{nullptr};
    SteadyClock::duration* time_index{nullptr};
};

[[nodiscard]] BlockConnectionTraceCounters BlockConnectionTraceCountersFor(ChainstateManager& chainman)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

class BlockConnectionTrace final
{
public:
    BlockConnectionTrace();
    explicit BlockConnectionTrace(BlockConnectionTraceCounters counters);

    void CountBlock();
    void SanityChecksDone();
    void ForkChecksDone();
    void SpendStageValidated(std::size_t transaction_count, int spend_inputs);
    void SpendStageCompleted(int spend_inputs);
    void UndoWritten();
    void IndexCommitted();

    [[nodiscard]] BlockConnectionStageTimings Timings() const;
    [[nodiscard]] std::chrono::nanoseconds TraceDuration() const;

private:
    [[nodiscard]] bool HasGlobalCounters() const noexcept;
    [[nodiscard]] int64_t GlobalBlockCount() const noexcept;

    BlockConnectionTraceCounters m_counters;
    SteadyClock::time_point m_start;
    SteadyClock::time_point m_after_sanity;
    SteadyClock::time_point m_after_forks;
    SteadyClock::time_point m_after_spend_validation;
    SteadyClock::time_point m_after_spend_completion;
    SteadyClock::time_point m_after_undo;
    SteadyClock::time_point m_after_index;
};

#endif // BITCOIN_VALIDATION_BLOCK_CONNECTION_TRACE_H
