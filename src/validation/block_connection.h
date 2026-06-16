// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_CONNECTION_H
#define BITCOIN_VALIDATION_BLOCK_CONNECTION_H

#include <consensus/block_check.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_spend.h>
#include <kernel/cs_main.h>

#include <memory>

class CBlock;
class CBlockIndex;
class BlockValidationState;
class BlockUndoWriter;
class BlockConnectionTrace;
class BlockIndexValidityCommitter;

namespace kernel {
class Notifications;
} // namespace kernel

/**
 * Block connection options.
 *
 * These keep block-check policy and commit behavior explicit at the validation
 * boundary. Script-cache policy belongs to the script-checker capability.
 */
struct BlockConnectionOptions {
    Consensus::BlockCheckOptions block_check_options{};
    bool commit{true};
};

namespace validation {

class BlockConnectionState;

/**
 * Consensus and policy context for a block connection attempt.
 *
 * Callers compute this before entering the engine so Core-specific policy
 * decisions do not stay hidden behind a broad runtime object.
 */
struct BlockConnectionContext {
    const Consensus::Params& consensus_params;
    Consensus::BlockConsensusContext consensus_context;
    std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times;
    Consensus::BlockSpendConsensusOptions spend_options;
};

/**
 * Runtime capabilities used by a block connection attempt.
 *
 * Each member is a specific effect boundary. Do not replace these with a broad
 * Chainstate or ChainstateManager reference; that makes local reasoning about
 * block connection effects harder.
 */
struct BlockConnectionRuntime {
    kernel::Notifications& notifications;
    BlockUndoWriter& undo_writer;
    BlockIndexValidityCommitter& block_index_committer;
    Consensus::BlockScriptChecker& script_checker;
    BlockConnectionTrace& trace;
};

/**
 * Block-connection request.
 *
 * This is still a Core validation request because it carries Core's current
 * block index. Spend-state reads and commits are behind BlockConnectionState so
 * alternate state implementations can run through the same engine.
 */
struct BlockConnectionRequest {
    BlockConnectionRuntime runtime;
    BlockConnectionContext context;
    const CBlock& block;
    CBlockIndex& block_index;
    BlockConnectionState& connection_state;
    BlockConnectionOptions options{};
};

enum class BlockConnectionStatus {
    Connected,
    Failed,
};

struct BlockConnectionResult {
    BlockConnectionStatus status{BlockConnectionStatus::Failed};

    [[nodiscard]] static BlockConnectionResult Connected() { return {BlockConnectionStatus::Connected}; }
    [[nodiscard]] static BlockConnectionResult Failed() { return {BlockConnectionStatus::Failed}; }
    [[nodiscard]] bool Succeeded() const { return status == BlockConnectionStatus::Connected; }
};

class BlockConnectionEngine final {
public:
    [[nodiscard]] BlockConnectionResult Connect(const BlockConnectionRequest& request, BlockValidationState& state) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

} // namespace validation

#endif // BITCOIN_VALIDATION_BLOCK_CONNECTION_H
