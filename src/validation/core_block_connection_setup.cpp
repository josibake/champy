// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_connection_setup.h>

#include <kernel/notifications_interface.h>
#include <primitives/block.h>

#include <utility>

CoreBlockConnectionSetup::CoreBlockConnectionSetup(CoreBlockConnectionRuntimeInputs runtime, CoreBlockConnectionPlan connection_plan, BlockConnectionTrace& trace, bool cache_script_results)
    : m_notifications{runtime.notifications},
      m_connection_plan{std::move(connection_plan)},
      m_script_checks{
          runtime.script_task_executor,
          m_connection_plan.script_check_decision.run_script_checks,
          cache_script_results,
          runtime.validation_cache,
          runtime.chain_lock},
      m_trace{trace}
{
}

validation::BlockConnectionRequest CoreBlockConnectionSetup::Request(const CBlock& block, validation::BlockConnectionState& connection_state, BlockConnectionOptions options)
{
    return {
        .runtime = {
            .notifications = m_notifications,
            .script_checker = m_script_checks.Checker(),
            .trace = m_trace,
        },
        .context = m_connection_plan.context,
        .block = block,
        .block_position = m_connection_plan.block_position,
        .connection_state = connection_state,
        .options = options,
    };
}
