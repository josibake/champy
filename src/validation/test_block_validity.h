// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_TEST_BLOCK_VALIDITY_H
#define BITCOIN_VALIDATION_TEST_BLOCK_VALIDITY_H

#include <kernel/cs_main.h>
#include <validation/block_validation.h>

#include <memory>

class BlockHeaderContextProvider;
class BlockIndexValidityCommitter;
class BlockUndoWriter;
class BlockConnectionTrace;
class CBlock;
namespace Consensus {
class BlockScriptChecker;
class SequenceLockTimeView;
} // namespace Consensus
namespace kernel {
class Notifications;
} // namespace kernel
namespace validation {
class ActiveChainView;
class BlockConnectionState;
} // namespace validation

struct TestBlockValidityRequest {
    validation::ActiveChainView& active_chain;
    const Consensus::Params& consensus_params;
    BlockHeaderContextProvider& header_context;
    validation::BlockConnectionState& connection_state;
    BlockUndoWriter& undo_writer;
    BlockIndexValidityCommitter& block_index_committer;
    kernel::Notifications& notifications;
    Consensus::BlockScriptChecker& script_checker;
    BlockConnectionTrace& trace;
    std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times{};
};

/**
 * Verify a block, including transactions. The block must connect to the current
 * tip of the supplied active chain.
 *
 * Returns a valid or invalid state. This does not currently return an error
 * state unless something is wrong with the existing chain state.
 */
BlockValidationState TestBlockValidity(
    TestBlockValidityRequest request,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_VALIDATION_TEST_BLOCK_VALIDITY_H
