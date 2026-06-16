// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SETUP_H
#define BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SETUP_H

#include <validation/block_connection.h>
#include <validation/block_connection_trace.h>
#include <validation/block_script_check_adapters.h>
#include <validation/core_block_connection_context.h>

class CBlock;
class CoreChainLock;
class ValidationCache;

namespace kernel {
class Notifications;
} // namespace kernel
namespace validation {
class BlockConnectionState;
} // namespace validation

struct CoreBlockConnectionRuntimeInputs {
    kernel::Notifications& notifications;
    validation::ScriptTaskExecutor& script_task_executor;
    ValidationCache& validation_cache;
    CoreChainLock* chain_lock{nullptr};
};

class CoreBlockConnectionSetup final
{
public:
    CoreBlockConnectionSetup(CoreBlockConnectionRuntimeInputs runtime, CoreBlockConnectionPlan connection_plan, BlockConnectionTrace& trace, bool cache_script_results);

    CoreBlockConnectionSetup(const CoreBlockConnectionSetup&) = delete;
    CoreBlockConnectionSetup& operator=(const CoreBlockConnectionSetup&) = delete;
    CoreBlockConnectionSetup(CoreBlockConnectionSetup&&) = delete;
    CoreBlockConnectionSetup& operator=(CoreBlockConnectionSetup&&) = delete;

    [[nodiscard]] validation::BlockConnectionRequest Request(const CBlock& block, validation::BlockConnectionState& connection_state, BlockConnectionOptions options = {});

private:
    kernel::Notifications& m_notifications;
    CoreBlockConnectionPlan m_connection_plan;
    CoreBlockScriptChecks m_script_checks;
    BlockConnectionTrace& m_trace;
};

#endif // BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SETUP_H
