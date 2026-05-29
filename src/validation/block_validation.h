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

#include <cstdint>
#include <memory>
#include <span>

class CBlockIndex;
class Chainstate;
class ChainstateEventSink;
class ChainstateManager;
class ValidationSignals;

struct FlatFilePos;

struct BlockHeaderAcceptanceOptions {
    bool min_pow_checked{false};
};

/**
 * Time-dependent block validation inputs.
 *
 * Keep these values explicit at validation entry points so tests and alternate
 * orchestrators can supply deterministic time without changing consensus
 * behavior.
 */
struct BlockValidationTime {
    int64_t current_time_seconds{0};
    int64_t max_future_block_time{0};
};

[[nodiscard]] BlockValidationTime CurrentBlockValidationTime();

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
bool RollforwardBlock(Chainstate& chainstate, const CBlockIndex* pindex, CCoinsViewCache& inputs) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
bool ReplayBlocks(Chainstate& chainstate);
void UpdateUncommittedBlockStructures(const ChainstateManager& chainman, CBlock& block, const CBlockIndex* pindexPrev);
void GenerateCoinbaseCommitment(const ChainstateManager& chainman, CBlock& block, const CBlockIndex* pindexPrev);

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

void LimitValidationInterfaceQueue(ValidationSignals& signals) LOCKS_EXCLUDED(cs_main);

#endif // BITCOIN_BLOCK_VALIDATION_H
