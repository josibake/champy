// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_H
#define BITCOIN_BLOCK_VALIDATION_H

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/amount.h>
#include <consensus/block_check.h>
#include <consensus/block_spend.h>
#include <consensus/params.h>
#include <primitives/block.h>
#include <util/check.h>
#include <util/time.h>
#include <validation/block_data_admission.h>
#include <validation/block_index_snapshot.h>
#include <validation/candidate_context.h>
#include <validation_state.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>

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

using NewBlockCandidateContextSnapshot = validation::AcceptedBlockContextSnapshot;

/**
 * Time-dependent block validation inputs.
 *
 * Keep these values explicit at validation entry points so tests and alternate
 * orchestrators can supply deterministic time without changing consensus
 * behavior.
 */
class BlockValidationTime
{
public:
    [[nodiscard]] static std::optional<BlockValidationTime> FromUnixSeconds(int64_t current_time_seconds) noexcept
    {
        if (current_time_seconds < 0) return std::nullopt;
        if (current_time_seconds > std::numeric_limits<int64_t>::max() - MAX_FUTURE_BLOCK_TIME) {
            return std::nullopt;
        }
        return BlockValidationTime{current_time_seconds};
    }

    [[nodiscard]] static BlockValidationTime FromCurrentTime(NodeSeconds current_time) noexcept
    {
        auto time{FromUnixSeconds(TicksSinceEpoch<std::chrono::seconds>(current_time))};
        Assume(time.has_value());
        return *time;
    }

    [[nodiscard]] int64_t CurrentTimeSeconds() const noexcept { return m_current_time_seconds; }
    [[nodiscard]] int64_t MaxFutureBlockTimeSeconds() const noexcept { return m_current_time_seconds + MAX_FUTURE_BLOCK_TIME; }
    [[nodiscard]] NodeSeconds CurrentTime() const noexcept { return NodeSeconds{std::chrono::seconds{m_current_time_seconds}}; }

private:
    explicit constexpr BlockValidationTime(int64_t current_time_seconds) noexcept
        : m_current_time_seconds{current_time_seconds}
    {
    }

    int64_t m_current_time_seconds;
};

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

class BlockAcceptanceResult
{
public:
    [[nodiscard]] static BlockAcceptanceResult HeaderRejected(std::optional<ChainWorkBlockSnapshot> block = std::nullopt) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::HeaderRejected, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult AlreadyKnown(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::BlockDataAlreadyKnown, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult UnrequestedPreviouslyProcessed(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::BlockDataUnrequestedPreviouslyProcessed, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult UnrequestedLessWorkThanTip(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::BlockDataUnrequestedLessWorkThanTip, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult UnrequestedTooFarAhead(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::BlockDataUnrequestedTooFarAhead, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult UnrequestedBelowMinimumChainWork(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::BlockDataUnrequestedBelowMinimumChainWork, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult BlockRejected(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::BlockRejected, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult StorageFailed(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::StorageFailed, std::move(block)};
    }

    [[nodiscard]] static BlockAcceptanceResult Stored(ChainWorkBlockSnapshot block) noexcept
    {
        return BlockAcceptanceResult{BlockAcceptanceStatus::BlockDataStored, std::move(block)};
    }

    [[nodiscard]] BlockAcceptanceStatus status() const noexcept { return m_status; }
    [[nodiscard]] const std::optional<ChainWorkBlockSnapshot>& block() const noexcept { return m_block; }

    [[nodiscard]] bool HasStoredBlockData() const noexcept
    {
        return m_status == BlockAcceptanceStatus::BlockDataStored;
    }

    [[nodiscard]] bool CanSnapshotAcceptedContext() const noexcept
    {
        switch (m_status) {
        case BlockAcceptanceStatus::BlockDataStored:
        case BlockAcceptanceStatus::BlockDataAlreadyKnown:
        case BlockAcceptanceStatus::BlockDataUnrequestedPreviouslyProcessed:
        case BlockAcceptanceStatus::BlockDataUnrequestedLessWorkThanTip:
        case BlockAcceptanceStatus::BlockDataUnrequestedTooFarAhead:
        case BlockAcceptanceStatus::BlockDataUnrequestedBelowMinimumChainWork:
            return m_block.has_value();
        case BlockAcceptanceStatus::HeaderRejected:
        case BlockAcceptanceStatus::BlockRejected:
        case BlockAcceptanceStatus::StorageFailed:
            return false;
        }
        assert(false);
        return false;
    }

    [[nodiscard]] bool ShouldAttemptActivation() const noexcept
    {
        return CanSnapshotAcceptedContext();
    }

    [[nodiscard]] bool IsStorageFailure() const noexcept
    {
        return m_status == BlockAcceptanceStatus::StorageFailed;
    }

private:
    explicit BlockAcceptanceResult(BlockAcceptanceStatus status, std::optional<ChainWorkBlockSnapshot> block = std::nullopt) noexcept
        : m_status{status}, m_block{std::move(block)}
    {
    }

    BlockAcceptanceStatus m_status{BlockAcceptanceStatus::HeaderRejected};
    std::optional<ChainWorkBlockSnapshot> m_block{};
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
    std::chrono::nanoseconds retire{0};
    std::chrono::nanoseconds commit{0};
    std::chrono::nanoseconds activation{0};
    std::chrono::nanoseconds total{0};
};

class NewBlockProcessingResult
{
public:
    NewBlockProcessingTimings timings{};
    uint64_t activated_blocks{0};
    uint64_t retired_blocks{0};
    uint64_t segment_blocks{0};

    [[nodiscard]] NewBlockProcessingStatus status() const noexcept { return m_status; }
    [[nodiscard]] BlockAcceptanceStatus block_acceptance_status() const noexcept { return m_block_acceptance_status; }
    [[nodiscard]] const BlockValidationState& validation_state() const noexcept { return m_validation_state; }
    [[nodiscard]] const std::optional<NewBlockCandidateContextSnapshot>& candidate_context() const noexcept { return m_candidate_context; }

    void MarkStructuralRejected(BlockValidationState state)
    {
        m_status = NewBlockProcessingStatus::BlockCheckFailed;
        m_block_acceptance_status = BlockAcceptanceStatus::HeaderRejected;
        m_validation_state = std::move(state);
        m_candidate_context.reset();
    }

    void MarkNotAccepted(const BlockAcceptanceResult& acceptance, BlockValidationState state)
    {
        assert(!acceptance.ShouldAttemptActivation());
        m_status = NewBlockProcessingStatus::BlockNotAccepted;
        m_block_acceptance_status = acceptance.status();
        m_validation_state = std::move(state);
        m_candidate_context.reset();
    }

    void MarkAcceptedCandidate(const BlockAcceptanceResult& acceptance)
    {
        assert(acceptance.ShouldAttemptActivation());
        m_status = NewBlockProcessingStatus::ActivationFailed;
        m_block_acceptance_status = acceptance.status();
        m_validation_state = {};
    }

    void SetCandidateContext(std::optional<NewBlockCandidateContextSnapshot> candidate_context)
    {
        assert(m_status == NewBlockProcessingStatus::ActivationFailed ||
               m_status == NewBlockProcessingStatus::Processed);
        m_candidate_context = std::move(candidate_context);
    }

    void MarkActivationFailed(BlockValidationState state = {})
    {
        assert(m_block_acceptance_status != BlockAcceptanceStatus::HeaderRejected);
        m_status = NewBlockProcessingStatus::ActivationFailed;
        m_validation_state = std::move(state);
    }

    void MarkProcessed()
    {
        assert(m_block_acceptance_status != BlockAcceptanceStatus::HeaderRejected);
        m_status = NewBlockProcessingStatus::Processed;
        m_validation_state = {};
    }

    [[nodiscard]] bool Processed() const noexcept
    {
        return m_status == NewBlockProcessingStatus::Processed;
    }

    [[nodiscard]] bool HasNewStoredBlockData() const noexcept
    {
        return m_block_acceptance_status == BlockAcceptanceStatus::BlockDataStored;
    }

    [[nodiscard]] bool BlockAdmissionAttempted() const noexcept
    {
        return m_status != NewBlockProcessingStatus::BlockCheckFailed;
    }

    [[nodiscard]] bool ActivationAttempted() const noexcept
    {
        return m_status == NewBlockProcessingStatus::ActivationFailed ||
               m_status == NewBlockProcessingStatus::Processed;
    }

private:
    NewBlockProcessingStatus m_status{NewBlockProcessingStatus::BlockCheckFailed};
    BlockAcceptanceStatus m_block_acceptance_status{BlockAcceptanceStatus::HeaderRejected};
    BlockValidationState m_validation_state{};
    std::optional<NewBlockCandidateContextSnapshot> m_candidate_context{};
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
