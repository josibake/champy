// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_connection_trace.h>

#include <chainstate.h>
#include <util/log.h>
#include <util/time.h>

#include <chrono>
#include <cstddef>

BlockConnectionTraceCounters BlockConnectionTraceCountersFor(ChainstateManager& chainman)
{
    return {
        .num_blocks_total = chainman.NumBlocksTotal(),
        .time_check = chainman.TimeCheck(),
        .time_forks = chainman.TimeForks(),
        .time_connect = chainman.TimeConnect(),
        .time_verify = chainman.TimeVerify(),
        .time_undo = chainman.TimeUndo(),
        .time_index = chainman.TimeIndex(),
    };
}

BlockConnectionTrace::BlockConnectionTrace(BlockConnectionTraceCounters counters)
    : m_counters{counters},
      m_start{SteadyClock::now()},
      m_after_sanity{m_start},
      m_after_forks{m_start},
      m_after_spend_validation{m_start},
      m_after_spend_completion{m_start},
      m_after_undo{m_start},
      m_after_index{m_start}
{
}

void BlockConnectionTrace::CountBlock()
{
    m_counters.num_blocks_total++;
}

void BlockConnectionTrace::SanityChecksDone()
{
    m_after_sanity = SteadyClock::now();
    m_counters.time_check += m_after_sanity - m_start;
    LogDebug(BCLog::BENCH, "    - Sanity checks: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_sanity - m_start),
             Ticks<SecondsDouble>(m_counters.time_check),
             Ticks<MillisecondsDouble>(m_counters.time_check) / m_counters.num_blocks_total);
}

void BlockConnectionTrace::ForkChecksDone()
{
    m_after_forks = SteadyClock::now();
    m_counters.time_forks += m_after_forks - m_after_sanity;
    LogDebug(BCLog::BENCH, "    - Fork checks: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_forks - m_after_sanity),
             Ticks<SecondsDouble>(m_counters.time_forks),
             Ticks<MillisecondsDouble>(m_counters.time_forks) / m_counters.num_blocks_total);
}

void BlockConnectionTrace::SpendStageValidated(std::size_t transaction_count, int spend_inputs)
{
    m_after_spend_validation = SteadyClock::now();
    m_counters.time_connect += m_after_spend_validation - m_after_forks;
    LogDebug(BCLog::BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs (%.2fms/blk)]\n",
             static_cast<unsigned>(transaction_count),
             Ticks<MillisecondsDouble>(m_after_spend_validation - m_after_forks),
             Ticks<MillisecondsDouble>(m_after_spend_validation - m_after_forks) / transaction_count,
             spend_inputs <= 1 ? 0 : Ticks<MillisecondsDouble>(m_after_spend_validation - m_after_forks) / (spend_inputs - 1),
             Ticks<SecondsDouble>(m_counters.time_connect),
             Ticks<MillisecondsDouble>(m_counters.time_connect) / m_counters.num_blocks_total);
}

void BlockConnectionTrace::SpendStageCompleted(int spend_inputs)
{
    m_after_spend_completion = SteadyClock::now();
    m_counters.time_verify += m_after_spend_completion - m_after_forks;
    LogDebug(BCLog::BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs (%.2fms/blk)]\n",
             spend_inputs - 1,
             Ticks<MillisecondsDouble>(m_after_spend_completion - m_after_forks),
             spend_inputs <= 1 ? 0 : Ticks<MillisecondsDouble>(m_after_spend_completion - m_after_forks) / (spend_inputs - 1),
             Ticks<SecondsDouble>(m_counters.time_verify),
             Ticks<MillisecondsDouble>(m_counters.time_verify) / m_counters.num_blocks_total);
}

void BlockConnectionTrace::UndoWritten()
{
    m_after_undo = SteadyClock::now();
    m_counters.time_undo += m_after_undo - m_after_spend_completion;
    LogDebug(BCLog::BENCH, "    - Write undo data: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_undo - m_after_spend_completion),
             Ticks<SecondsDouble>(m_counters.time_undo),
             Ticks<MillisecondsDouble>(m_counters.time_undo) / m_counters.num_blocks_total);
}

void BlockConnectionTrace::IndexCommitted()
{
    m_after_index = SteadyClock::now();
    m_counters.time_index += m_after_index - m_after_undo;
    LogDebug(BCLog::BENCH, "    - Index writing: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_index - m_after_undo),
             Ticks<SecondsDouble>(m_counters.time_index),
             Ticks<MillisecondsDouble>(m_counters.time_index) / m_counters.num_blocks_total);
}

std::chrono::nanoseconds BlockConnectionTrace::TraceDuration() const
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(m_after_undo - m_start);
}
