// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_DATA_ADAPTERS_H
#define BITCOIN_BLOCK_DATA_ADAPTERS_H

#include <consensus/block_commit.h>
#include <flatfile.h>
#include <kernel/cs_main.h>
#include <primitives/block.h>
#include <uint256.h>
#include <undo.h>

#include <optional>

class CBlockIndex;

namespace kernel {
class BlockManager;
} // namespace kernel

class BlockDataReader
{
public:
    virtual ~BlockDataReader() = default;

    virtual bool ReadBlock(CBlock& block, const CBlockIndex& index) = 0;
    virtual bool ReadBlockFromPosition(CBlock& block, const FlatFilePos& pos, const std::optional<uint256>& expected_hash) = 0;
};

class BlockUndoReader
{
public:
    virtual ~BlockUndoReader() = default;

    virtual bool ReadBlockUndo(CBlockUndo& blockundo, const CBlockIndex& index) = 0;
};

class BlockUndoWriter
{
public:
    virtual ~BlockUndoWriter() = default;

    virtual Consensus::BlockCommitResult<void> WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class BlockDataWriter
{
public:
    virtual ~BlockDataWriter() = default;

    virtual FlatFilePos WriteBlock(const CBlock& block, int height) = 0;
    virtual void UpdateBlockInfo(const CBlock& block, unsigned int height, const FlatFilePos& pos) = 0;
};

class CoreBlockDataStore final : public BlockDataReader, public BlockUndoReader, public BlockUndoWriter, public BlockDataWriter
{
public:
    explicit CoreBlockDataStore(kernel::BlockManager& blockman) : m_blockman{blockman} {}

    bool ReadBlock(CBlock& block, const CBlockIndex& index) override;
    bool ReadBlockFromPosition(CBlock& block, const FlatFilePos& pos, const std::optional<uint256>& expected_hash) override;
    bool ReadBlockUndo(CBlockUndo& blockundo, const CBlockIndex& index) override;
    Consensus::BlockCommitResult<void> WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool IsPruneMode() const;
    bool HasIndexedBlockFiles() const;
    FlatFilePos WriteBlock(const CBlock& block, int height) override;
    void UpdateBlockInfo(const CBlock& block, unsigned int height, const FlatFilePos& pos) override;

private:
    kernel::BlockManager& m_blockman;
};

#endif // BITCOIN_BLOCK_DATA_ADAPTERS_H
