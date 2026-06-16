// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_BLOCK_COMMIT_H
#define BITCOIN_CONSENSUS_BLOCK_COMMIT_H

#include <consensus/block_spend.h>
#include <uint256.h>

#include <cstdint>
#include <optional>
#include <string>

namespace Consensus {

enum class BlockCommitFailureState {
    // The failed commit operation left its externally visible commit target
    // unchanged. The caller may discard this block attempt and keep using the
    // target.
    Unchanged,
    // The failed commit operation may have published a prefix of the requested
    // effects, or failed while flushing a non-atomic backend. The caller must
    // treat the commit target as terminal for this attempt and tear it down or
    // reload it before continuing.
    Tainted,
};

struct BlockCommitError {
    std::optional<ValidationRuntimeIssue> runtime_issue{};
    BlockCommitFailureState failure_state{BlockCommitFailureState::Unchanged};
    std::string reject_reason;
};

struct BlockCommitContext {
    // Commit adapters use these facts to bind effects to a specific ordered
    // block transition. They must not infer block identity or height from
    // backend state.
    uint256 new_best_block;
    int block_height{0};
    int64_t previous_median_time_past{0};
};

template <typename T>
using BlockCommitResult = Consensus::Expected<T, BlockCommitError>;

class BlockRevertDataWriter
{
public:
    virtual ~BlockRevertDataWriter() = default;

    // Persist enough ordered spent-coin data to disconnect this block later.
    // Failure must state whether this writer left its target unchanged or
    // tainted.
    [[nodiscard]] virtual BlockCommitResult<void> WriteBlockRevertData(const BlockCommitContext& context, const BlockSpendEffects& effects) = 0;
};

class BlockMetadataCommitter
{
public:
    virtual ~BlockMetadataCommitter() = default;

    // Publish block metadata after revert data and spend state are durable.
    // Failure must state whether this committer left its target unchanged or
    // tainted.
    [[nodiscard]] virtual BlockCommitResult<void> CommitBlockMetadata(const BlockCommitContext& context, const BlockSpendEffects& effects) = 0;
};

class SpendCommitter
{
public:
    virtual ~SpendCommitter() = default;

    // Apply the ordered spend/create effects to the backend's committed state.
    // Pre-publication validation failures should return Unchanged. Once an
    // implementation begins publishing to its parent/durable backend, any
    // subsequent failure must return Tainted.
    [[nodiscard]] virtual BlockCommitResult<void> CommitSpendState(const BlockCommitContext& context, const BlockSpendEffects& effects) = 0;
};

using BlockSpendStateCommitter = SpendCommitter;

// Commits block side effects in order: revert data, spend state, metadata. A
// failed first step may be Unchanged. Any failure after an earlier step
// succeeded is returned as Tainted because the whole block commit has already
// published a prefix of its effects.
[[nodiscard]] BlockCommitResult<void> CommitBlockEffects(const BlockCommitContext& context, const BlockSpendEffects& effects, BlockRevertDataWriter& revert_data_writer, SpendCommitter& spend_state_committer, BlockMetadataCommitter& metadata_committer);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_BLOCK_COMMIT_H
