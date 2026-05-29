// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_setup.h>

#include <kernel/notifications_interface.h>
#include <primitives/block.h>
#include <uint256.h>

#include <utility>

CoreBlockConnectionSetup::CoreBlockConnectionSetup(CoreBlockConnectionRuntimeInputs runtime, CoreBlockConnectionPlan connection_plan, CBlockIndex& block_index, bool cache_script_results)
    : m_notifications{runtime.notifications},
      m_block_index{block_index},
      m_undo_writer{runtime.undo_writer},
      m_block_index_committer{runtime.block_index_committer},
      m_connection_plan{std::move(connection_plan)},
      m_script_checks{
          runtime.script_check_queue,
          m_connection_plan.script_check_decision.run_script_checks,
          cache_script_results,
          runtime.validation_cache},
      m_trace{runtime.trace_counters}
{
}

void CoreBlockConnectionSetup::MaybeLogScriptPolicy(std::optional<const char*>& last_reason_logged, const uint256& block_hash) const
{
    MaybeLogCoreBlockConnectionScriptPolicy(last_reason_logged, m_block_index, block_hash, m_connection_plan);
}

validation::BlockConnectionRequest CoreBlockConnectionSetup::Request(const CBlock& block, CCoinsViewCache& coins_view, BlockConnectionOptions options)
{
    return {
        .runtime = {
            .notifications = m_notifications,
            .undo_writer = m_undo_writer,
            .block_index_committer = m_block_index_committer,
            .script_checker = m_script_checks.Checker(),
            .trace = m_trace,
        },
        .context = m_connection_plan.context,
        .block = block,
        .block_index = m_block_index,
        .coins_view = coins_view,
        .options = options,
    };
}
