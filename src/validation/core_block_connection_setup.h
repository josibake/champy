// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SETUP_H
#define BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SETUP_H

#include <kernel/cs_main.h>
#include <validation/block_connection.h>
#include <validation/block_connection_trace.h>
#include <validation/block_script_check_adapters.h>
#include <validation/core_block_connection_context.h>

#include <optional>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class BlockUndoWriter;
class BlockIndexValidityCommitter;
class ValidationCache;
class uint256;

namespace kernel {
class Notifications;
} // namespace kernel

struct CoreBlockConnectionRuntimeInputs {
    kernel::Notifications& notifications;
    BlockUndoWriter& undo_writer;
    BlockIndexValidityCommitter& block_index_committer;
    CCheckQueue<CScriptCheck>& script_check_queue;
    ValidationCache& validation_cache;
    BlockConnectionTraceCounters trace_counters;
};

class CoreBlockConnectionSetup final {
public:
    CoreBlockConnectionSetup(CoreBlockConnectionRuntimeInputs runtime, CoreBlockConnectionPlan connection_plan, CBlockIndex& block_index, bool cache_script_results)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    CoreBlockConnectionSetup(const CoreBlockConnectionSetup&) = delete;
    CoreBlockConnectionSetup& operator=(const CoreBlockConnectionSetup&) = delete;
    CoreBlockConnectionSetup(CoreBlockConnectionSetup&&) = delete;
    CoreBlockConnectionSetup& operator=(CoreBlockConnectionSetup&&) = delete;

    void MaybeLogScriptPolicy(std::optional<const char*>& last_reason_logged, const uint256& block_hash) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    [[nodiscard]] validation::BlockConnectionRequest Request(const CBlock& block, CCoinsViewCache& coins_view, BlockConnectionOptions options = {}) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    kernel::Notifications& m_notifications;
    CBlockIndex& m_block_index;
    BlockUndoWriter& m_undo_writer;
    BlockIndexValidityCommitter& m_block_index_committer;
    CoreBlockConnectionPlan m_connection_plan;
    CoreBlockScriptChecks m_script_checks;
    BlockConnectionTrace m_trace;
};

#endif // BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SETUP_H
