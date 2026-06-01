// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_CONNECTION_H
#define BITCOIN_VALIDATION_BLOCK_CONNECTION_H

#include <consensus/block_check.h>
#include <consensus/block_commit.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_spend.h>
#include <kernel/cs_main.h>
#include <validation/block_connection_state.h>
#include <uint256.h>

#include <memory>
#include <optional>
#include <utility>

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
 * These keep block-check policy explicit at the validation boundary.
 * Script-cache policy belongs to the script-checker capability.
 */
struct BlockConnectionOptions {
    Consensus::BlockCheckOptions block_check_options{};
};

namespace validation {

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
    Consensus::BlockScriptChecker& script_checker;
    BlockConnectionTrace& trace;
    const Consensus::BlockSpendJoiner* spend_joiner{nullptr};
};

struct BlockConnectionBlockPosition {
    uint256 hash{};
    uint256 parent_hash{};
    int height{-1};
};

[[nodiscard]] BlockConnectionBlockPosition SnapshotBlockConnectionPosition(const CBlockIndex& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

/**
 * Block-connection request.
 *
 * Execution receives copied block-position facts, not Core's mutable block
 * index. Spend-state reads and commits are behind BlockConnectionState so
 * alternate state implementations can run through the same engine.
 */
struct BlockConnectionRequest {
    BlockConnectionRuntime runtime;
    BlockConnectionContext context;
    const CBlock& block;
    BlockConnectionBlockPosition block_position;
    BlockConnectionState& connection_state;
    BlockConnectionOptions options{};
};

struct BlockConnectionCommitRuntime {
    BlockUndoWriter& undo_writer;
    BlockIndexValidityCommitter& block_index_committer;
    Consensus::BlockSpendStateCommitter& spend_state_committer;
    BlockConnectionTrace& trace;
};

struct BlockConnectionCommitContext {
    const CBlock& block;
    CBlockIndex& block_index;
    BlockConnectionState& connection_state;
};

struct BlockConnectionCommitRequest {
    BlockConnectionCommitRuntime runtime;
    BlockConnectionCommitContext context;
};

enum class BlockConnectionStatus {
    Connected,
    Failed,
};

struct BlockConnectionCommitPackage {
    uint256 expected_previous_block;
    Consensus::BlockCommitContext commit_context;
    std::optional<Consensus::BlockSpendEffects> effects;
};

struct BlockConnectionResult {
    BlockConnectionStatus status{BlockConnectionStatus::Failed};
    std::optional<BlockConnectionCommitPackage> commit_package{};

    [[nodiscard]] static BlockConnectionResult Connected() { return {BlockConnectionStatus::Connected}; }
    [[nodiscard]] static BlockConnectionResult Connected(BlockConnectionCommitPackage package) { return {BlockConnectionStatus::Connected, std::move(package)}; }
    [[nodiscard]] static BlockConnectionResult Failed() { return {BlockConnectionStatus::Failed}; }
    [[nodiscard]] bool Succeeded() const { return status == BlockConnectionStatus::Connected; }
};

class BlockConnectionEngine final {
public:
    [[nodiscard]] BlockConnectionResult ConnectPrepared(const BlockConnectionRequest& request, BlockValidationState& state) const;
    [[nodiscard]] BlockConnectionResult Commit(const BlockConnectionCommitRequest& request, BlockConnectionCommitPackage package, BlockValidationState& state) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

} // namespace validation

#endif // BITCOIN_VALIDATION_BLOCK_CONNECTION_H
