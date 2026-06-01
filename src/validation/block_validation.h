// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_H
#define BITCOIN_BLOCK_VALIDATION_H

#include <arith_uint256.h>
#include <consensus/amount.h>
#include <consensus/block_check.h>
#include <consensus/block_spend.h>
#include <consensus/params.h>
#include <validation/block_index_snapshot.h>
#include <validation/block_data_admission.h>
#include <validation_state.h>
#include <primitives/block.h>

#include <cstdint>
#include <chrono>
#include <memory>
#include <optional>
#include <span>

struct FlatFilePos;

struct BlockHeaderAcceptanceOptions {
    bool min_pow_checked{false};
};

struct BlockStructuralCheckProof {
    uint256 block_hash;

    [[nodiscard]] bool Matches(const CBlock& block) const { return block_hash == block.GetHash(); }
};

struct NewBlockStructuralCheckResult {
    std::optional<BlockStructuralCheckProof> proof{};

    [[nodiscard]] bool passed() const noexcept { return proof.has_value(); }
};

struct NewBlockCandidateContextSnapshot {
    ChainWorkBlockSnapshot block{};
    uint256 previous_block_hash{};
    int previous_block_height{-1};
    int64_t previous_median_time_past{0};
    int64_t previous_block_time{0};
    Consensus::BlockDeploymentContext deployments{};
    Consensus::BlockSpendConsensusOptions spend_options{};
    CAmount block_subsidy{0};
    bool has_spend_stage{false};

    [[nodiscard]] bool Matches(const CBlock& candidate) const { return block.hash == candidate.GetHash(); }
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
    BlockDataStorageMode block_data_storage{BlockDataStorageMode::ApplyAdmissionChecks};
    const FlatFilePos* existing_block_pos{nullptr};
    BlockHeaderAcceptanceOptions header{};
    std::optional<BlockStructuralCheckProof> structural_check{};
};

struct NewBlockProcessingOptions {
    BlockDataStorageMode block_data_storage{BlockDataStorageMode::ApplyAdmissionChecks};
    BlockHeaderAcceptanceOptions header{};
    std::optional<BlockStructuralCheckProof> structural_check{};
};

struct NewBlockHeadersResult {
    bool accepted{false};
    std::optional<AcceptedBlockHeaderSnapshot> last_accepted{};
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
    std::optional<ChainWorkBlockSnapshot> block{};

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

struct BlockActivationTimings {
    std::chrono::nanoseconds spend_join{0};
    std::chrono::nanoseconds script_validation{0};
};

enum class BlockActivationStatus {
    Completed,
    SystemError,
};

struct BlockActivationResult {
    BlockActivationStatus status{BlockActivationStatus::SystemError};
    BlockActivationTimings timings{};
    uint64_t connected_blocks{0};

    [[nodiscard]] static BlockActivationResult Completed(BlockActivationTimings timings = {}, uint64_t connected_blocks = 0) noexcept
    {
        return {BlockActivationStatus::Completed, timings, connected_blocks};
    }

    [[nodiscard]] static BlockActivationResult SystemError(BlockActivationTimings timings = {}, uint64_t connected_blocks = 0) noexcept
    {
        return {BlockActivationStatus::SystemError, timings, connected_blocks};
    }

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return status == BlockActivationStatus::Completed;
    }
};

struct NewBlockProcessingTimings {
    std::chrono::nanoseconds structural_check{0};
    std::chrono::nanoseconds block_acceptance{0};
    std::chrono::nanoseconds context_snapshot{0};
    std::chrono::nanoseconds spend_join{0};
    std::chrono::nanoseconds script_validation{0};
    std::chrono::nanoseconds activation{0};
    std::chrono::nanoseconds total{0};
};

struct NewBlockProcessingResult {
    NewBlockProcessingStatus status{NewBlockProcessingStatus::BlockCheckFailed};
    BlockAcceptanceStatus block_acceptance_status{BlockAcceptanceStatus::HeaderRejected};
    NewBlockProcessingTimings timings{};
    uint64_t activated_blocks{0};
    std::optional<NewBlockCandidateContextSnapshot> candidate_context{};

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

#endif // BITCOIN_BLOCK_VALIDATION_H
