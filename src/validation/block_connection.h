// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_CONNECTION_H
#define BITCOIN_VALIDATION_BLOCK_CONNECTION_H

#include <consensus/block_check.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_spend.h>
#include <kernel/cs_main.h>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class BlockValidationState;
class BlockDataStore;
class BlockConnectionTrace;
class BlockIndexStore;

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

/**
 * Consensus and policy context for a block connection attempt.
 *
 * Callers compute this before entering the engine so Core-specific policy
 * decisions do not stay hidden behind a broad runtime object.
 */
struct BlockConnectionContext {
    const Consensus::Params& consensus_params;
    Consensus::BlockConsensusContext consensus_context;
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
    BlockDataStore& block_store;
    BlockIndexStore& block_index_store;
    Consensus::BlockScriptChecker& script_checker;
    BlockConnectionTrace& trace;
};

/**
 * Core block-connection request.
 *
 * This is still a Core validation request: it carries Core's current block
 * index, coins cache, and runtime capabilities. The request keeps those
 * dependencies local while block connection moves toward a smaller validation
 * engine.
 */
struct BlockConnectionRequest {
    BlockConnectionRuntime runtime;
    BlockConnectionContext context;
    const CBlock& block;
    CBlockIndex& block_index;
    CCoinsViewCache& coins_view;
    BlockConnectionOptions options{};
};

class BlockConnectionEngine final {
public:
    [[nodiscard]] bool Connect(const BlockConnectionRequest& request, BlockValidationState& state) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

} // namespace validation

#endif // BITCOIN_VALIDATION_BLOCK_CONNECTION_H
