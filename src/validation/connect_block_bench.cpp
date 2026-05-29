// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/connect_block_bench.h>

#include <chainstate.h>
#include <util/log.h>
#include <util/time.h>

#include <chrono>
#include <cstddef>

ConnectBlockBench::ConnectBlockBench(ChainstateManager& chainman)
    : m_chainman{chainman},
      m_start{SteadyClock::now()},
      m_after_sanity{m_start},
      m_after_forks{m_start},
      m_after_spend_validation{m_start},
      m_after_spend_completion{m_start},
      m_after_undo{m_start},
      m_after_index{m_start}
{
}

void ConnectBlockBench::CountBlock()
{
    m_chainman.NumBlocksTotal()++;
}

void ConnectBlockBench::SanityChecksDone()
{
    m_after_sanity = SteadyClock::now();
    m_chainman.TimeCheck() += m_after_sanity - m_start;
    LogDebug(BCLog::BENCH, "    - Sanity checks: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_sanity - m_start),
             Ticks<SecondsDouble>(m_chainman.TimeCheck()),
             Ticks<MillisecondsDouble>(m_chainman.TimeCheck()) / m_chainman.NumBlocksTotal());
}

void ConnectBlockBench::ForkChecksDone()
{
    m_after_forks = SteadyClock::now();
    m_chainman.TimeForks() += m_after_forks - m_after_sanity;
    LogDebug(BCLog::BENCH, "    - Fork checks: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_forks - m_after_sanity),
             Ticks<SecondsDouble>(m_chainman.TimeForks()),
             Ticks<MillisecondsDouble>(m_chainman.TimeForks()) / m_chainman.NumBlocksTotal());
}

void ConnectBlockBench::SpendStageValidated(std::size_t transaction_count, int spend_inputs)
{
    m_after_spend_validation = SteadyClock::now();
    m_chainman.TimeConnect() += m_after_spend_validation - m_after_forks;
    LogDebug(BCLog::BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs (%.2fms/blk)]\n",
             static_cast<unsigned>(transaction_count),
             Ticks<MillisecondsDouble>(m_after_spend_validation - m_after_forks),
             Ticks<MillisecondsDouble>(m_after_spend_validation - m_after_forks) / transaction_count,
             spend_inputs <= 1 ? 0 : Ticks<MillisecondsDouble>(m_after_spend_validation - m_after_forks) / (spend_inputs - 1),
             Ticks<SecondsDouble>(m_chainman.TimeConnect()),
             Ticks<MillisecondsDouble>(m_chainman.TimeConnect()) / m_chainman.NumBlocksTotal());
}

void ConnectBlockBench::SpendStageCompleted(int spend_inputs)
{
    m_after_spend_completion = SteadyClock::now();
    m_chainman.TimeVerify() += m_after_spend_completion - m_after_forks;
    LogDebug(BCLog::BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs (%.2fms/blk)]\n",
             spend_inputs - 1,
             Ticks<MillisecondsDouble>(m_after_spend_completion - m_after_forks),
             spend_inputs <= 1 ? 0 : Ticks<MillisecondsDouble>(m_after_spend_completion - m_after_forks) / (spend_inputs - 1),
             Ticks<SecondsDouble>(m_chainman.TimeVerify()),
             Ticks<MillisecondsDouble>(m_chainman.TimeVerify()) / m_chainman.NumBlocksTotal());
}

void ConnectBlockBench::UndoWritten()
{
    m_after_undo = SteadyClock::now();
    m_chainman.TimeUndo() += m_after_undo - m_after_spend_completion;
    LogDebug(BCLog::BENCH, "    - Write undo data: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_undo - m_after_spend_completion),
             Ticks<SecondsDouble>(m_chainman.TimeUndo()),
             Ticks<MillisecondsDouble>(m_chainman.TimeUndo()) / m_chainman.NumBlocksTotal());
}

void ConnectBlockBench::IndexCommitted()
{
    m_after_index = SteadyClock::now();
    m_chainman.TimeIndex() += m_after_index - m_after_undo;
    LogDebug(BCLog::BENCH, "    - Index writing: %.2fms [%.2fs (%.2fms/blk)]\n",
             Ticks<MillisecondsDouble>(m_after_index - m_after_undo),
             Ticks<SecondsDouble>(m_chainman.TimeIndex()),
             Ticks<MillisecondsDouble>(m_chainman.TimeIndex()) / m_chainman.NumBlocksTotal());
}

std::chrono::nanoseconds ConnectBlockBench::TraceDuration() const
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(m_after_undo - m_start);
}
