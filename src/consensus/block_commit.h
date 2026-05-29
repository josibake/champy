// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_BLOCK_COMMIT_H
#define BITCOIN_CONSENSUS_BLOCK_COMMIT_H

#include <consensus/block_spend.h>
#include <uint256.h>

#include <cstdint>
#include <string>

namespace Consensus {

struct BlockCommitError {
    std::string reject_reason;
};

struct BlockCommitContext {
    uint256 new_best_block;
    int block_height{0};
    int64_t previous_median_time_past{0};
};

template <typename T>
using BlockCommitResult = Consensus::Expected<T, BlockCommitError>;

class BlockRevertDataWriter {
public:
    virtual ~BlockRevertDataWriter() = default;

    [[nodiscard]] virtual BlockCommitResult<void> WriteBlockRevertData(const BlockCommitContext& context, const BlockSpendEffects& effects) = 0;
};

class BlockMetadataCommitter {
public:
    virtual ~BlockMetadataCommitter() = default;

    [[nodiscard]] virtual BlockCommitResult<void> CommitBlockMetadata(const BlockCommitContext& context, const BlockSpendEffects& effects) = 0;
};

class BlockSpendStateCommitter {
public:
    virtual ~BlockSpendStateCommitter() = default;

    [[nodiscard]] virtual BlockCommitResult<void> CommitSpendState(const BlockCommitContext& context, const BlockSpendEffects& effects) = 0;
};

[[nodiscard]] BlockCommitResult<void> CommitBlockEffects(const BlockCommitContext& context, const BlockSpendEffects& effects, BlockRevertDataWriter& revert_data_writer, BlockSpendStateCommitter& spend_state_committer, BlockMetadataCommitter& metadata_committer);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_BLOCK_COMMIT_H
