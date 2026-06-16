// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_BLOCK_COMMIT_ADAPTERS_H
#define BITCOIN_CORE_BLOCK_COMMIT_ADAPTERS_H

#include <consensus/block_commit.h>
#include <kernel/cs_main.h>

class BlockUndoWriter;
class CBlockIndex;
class CCoinsViewCache;
class BlockIndexValidityCommitter;
namespace validation {
struct BlockConnectionBlockPosition;
class BlockConnectionState;
} // namespace validation

class CoreBlockRevertDataWriter final : public Consensus::BlockRevertDataWriter {
public:
    CoreBlockRevertDataWriter(BlockUndoWriter& undo_writer, CBlockIndex& block_index);

    [[nodiscard]] Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    BlockUndoWriter& m_undo_writer;
    CBlockIndex& m_block_index;
};

class CoreBlockMetadataCommitter final : public Consensus::BlockMetadataCommitter {
public:
    CoreBlockMetadataCommitter(BlockIndexValidityCommitter& block_index_committer, validation::BlockConnectionState& connection_state, CBlockIndex& block_index);

    [[nodiscard]] Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    BlockIndexValidityCommitter& m_block_index_committer;
    validation::BlockConnectionState& m_connection_state;
    CBlockIndex& m_block_index;
};

class CoreBlockConnectionCommitTarget final {
public:
    CoreBlockConnectionCommitTarget(BlockUndoWriter& undo_writer, BlockIndexValidityCommitter& block_index_committer, validation::BlockConnectionState& connection_state, CBlockIndex& block_index);

    [[nodiscard]] Consensus::BlockRevertDataWriter& RevertDataWriter();
    [[nodiscard]] Consensus::BlockMetadataCommitter& MetadataCommitter();
    [[nodiscard]] validation::BlockConnectionBlockPosition BlockPosition() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    CBlockIndex& m_block_index;
    CoreBlockRevertDataWriter m_revert_data_writer;
    CoreBlockMetadataCommitter m_metadata_committer;
};

class CoreBlockSpendEffectsCommitter final : public Consensus::SpendCommitter {
public:
    explicit CoreBlockSpendEffectsCommitter(CCoinsViewCache& view);

    [[nodiscard]] Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    CCoinsViewCache& m_view;
};

#endif // BITCOIN_CORE_BLOCK_COMMIT_ADAPTERS_H
