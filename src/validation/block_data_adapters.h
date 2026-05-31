// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_DATA_ADAPTERS_H
#define BITCOIN_BLOCK_DATA_ADAPTERS_H

#include <validation/block_storage.h>

class CBlockIndex;

namespace kernel {
class BlockManager;
} // namespace kernel

class CoreBlockDataStore final : public BlockDataReader, public BlockUndoReader, public BlockUndoWriter, public BlockDataWriter, public BlockDataAvailability
{
public:
    explicit CoreBlockDataStore(kernel::BlockManager& blockman) : m_blockman{blockman} {}

    bool ReadBlock(CBlock& block, const CBlockIndex& index) override;
    bool ReadBlockFromPosition(CBlock& block, const FlatFilePos& pos, const std::optional<uint256>& expected_hash) override;
    bool ReadBlockUndo(CBlockUndo& blockundo, const CBlockIndex& index) override;
    Consensus::BlockCommitResult<void> WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool IsPruneMode() const override;
    bool HasIndexedBlockFiles() const;
    FlatFilePos WriteBlock(const CBlock& block, int height) override;
    void UpdateBlockInfo(const CBlock& block, unsigned int height, const FlatFilePos& pos) override;

private:
    kernel::BlockManager& m_blockman;
};

#endif // BITCOIN_BLOCK_DATA_ADAPTERS_H
