// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_commit.h>

namespace Consensus {

BlockCommitResult<void> CommitBlockEffects(const BlockCommitContext& context, const BlockSpendEffects& effects, BlockRevertDataWriter& revert_data_writer, BlockSpendStateCommitter& spend_state_committer, BlockMetadataCommitter& metadata_committer)
{
    if (const auto revert_write{revert_data_writer.WriteBlockRevertData(context, effects)}; !revert_write) {
        return Consensus::Unexpected<BlockCommitError>{revert_write.error()};
    }

    if (const auto spend_state_commit{spend_state_committer.CommitSpendState(context, effects)}; !spend_state_commit) {
        return Consensus::Unexpected<BlockCommitError>{spend_state_commit.error()};
    }

    if (const auto metadata_commit{metadata_committer.CommitBlockMetadata(context, effects)}; !metadata_commit) {
        return Consensus::Unexpected<BlockCommitError>{metadata_commit.error()};
    }

    return {};
}

} // namespace Consensus
