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

struct BlockConnectionTraceCounters {
    int64_t& num_blocks_total;
    SteadyClock::duration& time_check;
    SteadyClock::duration& time_forks;
    SteadyClock::duration& time_connect;
    SteadyClock::duration& time_verify;
    SteadyClock::duration& time_undo;
    SteadyClock::duration& time_index;
};

[[nodiscard]] BlockConnectionTraceCounters BlockConnectionTraceCountersFor(ChainstateManager& chainman)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

class BlockConnectionTrace final
{
public:
    explicit BlockConnectionTrace(BlockConnectionTraceCounters counters);

    void CountBlock() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void SanityChecksDone() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void ForkChecksDone() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void SpendStageValidated(std::size_t transaction_count, int spend_inputs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void SpendStageCompleted(int spend_inputs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void UndoWritten() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void IndexCommitted() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    [[nodiscard]] std::chrono::nanoseconds TraceDuration() const;

private:
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
