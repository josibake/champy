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

class CoreBlockEffectsWriter final : public Consensus::BlockRevertDataWriter, public Consensus::BlockMetadataCommitter {
public:
    CoreBlockEffectsWriter(BlockUndoWriter& undo_writer, BlockIndexValidityCommitter& block_index_committer, CCoinsViewCache& view, CBlockIndex& block_index);

    [[nodiscard]] Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    BlockUndoWriter& m_undo_writer;
    BlockIndexValidityCommitter& m_block_index_committer;
    CCoinsViewCache& m_view;
    CBlockIndex& m_block_index;
};

class CoreBlockSpendStateCommitter final : public Consensus::BlockSpendStateCommitter {
public:
    CoreBlockSpendStateCommitter(CCoinsViewCache& staged_view, CCoinsViewCache& view);

    [[nodiscard]] Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    CCoinsViewCache& m_staged_view;
    CCoinsViewCache& m_view;
};

#endif // BITCOIN_CORE_BLOCK_COMMIT_ADAPTERS_H
