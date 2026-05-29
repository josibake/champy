// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_H
#define BITCOIN_BLOCK_VALIDATION_H

#include <arith_uint256.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/block_check.h>
#include <consensus/params.h>
#include <validation_state.h>
#include <kernel/cs_main.h>
#include <primitives/block.h>

#include <memory>
#include <span>

class CBlockIndex;
class Chainstate;
class ChainstateMempoolSync;
class ChainstateManager;
class ValidationSignals;

struct FlatFilePos;

/**
 * ConnectBlock adapter options.
 *
 * These keep block-check policy, script-cache policy, and commit behavior
 * explicit at Core's existing validation entry point.
 */
struct ConnectBlockOptions {
    Consensus::BlockCheckOptions block_check_options{};
    bool cache_script_results{false};
    bool commit{true};
};

struct BlockHeaderAcceptanceOptions {
    bool min_pow_checked{false};
};

struct BlockAcceptanceOptions {
    bool block_data_requested{false};
    const FlatFilePos* existing_block_pos{nullptr};
    BlockHeaderAcceptanceOptions header{};
};

struct NewBlockProcessingOptions {
    bool force_processing{false};
    BlockHeaderAcceptanceOptions header{};
};

struct BlockHeaderAcceptanceResult {
    bool accepted{false};
    CBlockIndex* block_index{nullptr};
};

struct NewBlockHeadersResult {
    bool accepted{false};
    const CBlockIndex* last_accepted{nullptr};
};

enum class BlockAcceptanceStatus {
    HeaderRejected,
    BlockDataAlreadyKnown,
    BlockDataUnrequestedPreviouslyProcessed,
    BlockDataUnrequestedLessWorkThanTip,
    BlockDataUnrequestedTooFarAhead,
    BlockDataUnrequestedBelowMinimumChainWork,
    BlockRejected,
    StorageFailed,
    BlockDataStored,
};

struct BlockAcceptanceResult {
    BlockAcceptanceStatus status{BlockAcceptanceStatus::HeaderRejected};
    CBlockIndex* block_index{nullptr};

    [[nodiscard]] bool accepted_for_processing() const noexcept
    {
        return status == BlockAcceptanceStatus::BlockDataStored ||
               status == BlockAcceptanceStatus::BlockDataAlreadyKnown ||
               status == BlockAcceptanceStatus::BlockDataUnrequestedPreviouslyProcessed ||
               status == BlockAcceptanceStatus::BlockDataUnrequestedLessWorkThanTip ||
               status == BlockAcceptanceStatus::BlockDataUnrequestedTooFarAhead ||
               status == BlockAcceptanceStatus::BlockDataUnrequestedBelowMinimumChainWork;
    }

    [[nodiscard]] bool stored_block_data() const noexcept
    {
        return status == BlockAcceptanceStatus::BlockDataStored;
    }
};

enum class NewBlockProcessingStatus {
    BlockCheckFailed,
    BlockNotAccepted,
    ActivationFailed,
    Processed,
};

struct NewBlockProcessingResult {
    NewBlockProcessingStatus status{NewBlockProcessingStatus::BlockCheckFailed};
    BlockAcceptanceStatus block_acceptance_status{BlockAcceptanceStatus::HeaderRejected};

    [[nodiscard]] bool processed() const noexcept
    {
        return status == NewBlockProcessingStatus::Processed;
    }

    [[nodiscard]] bool new_block() const noexcept
    {
        return block_acceptance_status == BlockAcceptanceStatus::BlockDataStored;
    }
};

struct BlockMutationOptions {
    bool check_witness_root{false};
};

DisconnectResult DisconnectBlock(Chainstate& chainstate, const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool ConnectBlock(Chainstate& chainstate, const CBlock& block, BlockValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, ConnectBlockOptions options = {}) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
bool RollforwardBlock(Chainstate& chainstate, const CBlockIndex* pindex, CCoinsViewCache& inputs) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
bool ReplayBlocks(Chainstate& chainstate);
void UpdateUncommittedBlockStructures(const ChainstateManager& chainman, CBlock& block, const CBlockIndex* pindexPrev);
void GenerateCoinbaseCommitment(const ChainstateManager& chainman, CBlock& block, const CBlockIndex* pindexPrev);
[[nodiscard]] NewBlockHeadersResult ProcessNewBlockHeaders(ChainstateManager& chainman, std::span<const CBlockHeader> headers, BlockHeaderAcceptanceOptions options, BlockValidationState& state) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] BlockAcceptanceResult AcceptBlock(ChainstateManager& chainman, const std::shared_ptr<const CBlock>& pblock, BlockValidationState& state, BlockAcceptanceOptions options) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(ChainstateManager& chainman, ChainstateMempoolSync* mempool_sync, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options) LOCKS_EXCLUDED(cs_main);
[[nodiscard]] NewBlockProcessingResult ProcessNewBlock(ChainstateManager& chainman, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options) LOCKS_EXCLUDED(cs_main);

/** Context-independent validity checks */
bool CheckBlock(const CBlock& block, BlockValidationState& state, const Consensus::Params& consensusParams, const Consensus::BlockCheckOptions& options = {});

/** Check that the proof of work on each block header matches the value in nBits */
bool HasValidProofOfWork(std::span<const CBlockHeader> headers, const Consensus::Params& consensusParams);

/** Compute the block subsidy at a given height. */
CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams);

/** Check if a block has been mutated (with respect to its merkle root and witness commitments). */
bool IsBlockMutated(const CBlock& block, BlockMutationOptions options);

/** Return the sum of the claimed work on a given set of headers. No verification of PoW is done. */
arith_uint256 CalculateClaimedHeadersWork(std::span<const CBlockHeader> headers);

/**
 * Verify a block, including transactions.
 *
 * @param[in]   block       The block we want to process. Must connect to the
 *                          current tip.
 * @param[in]   chainstate  The chainstate to connect to.
 * @param[in]   options     Context-free block checks to run before contextual
 *                          validation. Header nBits is always checked.
 *
 * @return Valid or Invalid state. This doesn't currently return an Error state,
 *         and shouldn't unless there is something wrong with the existing
 *         chainstate. (This is different from functions like AcceptBlock which
 *         can fail trying to save new data.)
 *
 * For signets the challenge verification is skipped when check_pow is false.
 */
BlockValidationState TestBlockValidity(
    Chainstate& chainstate,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

void LimitValidationInterfaceQueue(ValidationSignals& signals) LOCKS_EXCLUDED(cs_main);

#endif // BITCOIN_BLOCK_VALIDATION_H
