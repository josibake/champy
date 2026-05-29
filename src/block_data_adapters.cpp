// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <block_data_adapters.h>

#include <node/blockstorage.h>

bool CoreBlockDataStore::ReadBlock(CBlock& block, const CBlockIndex& index)
{
    return m_blockman.ReadBlock(block, index);
}

bool CoreBlockDataStore::ReadBlockFromPosition(CBlock& block, const FlatFilePos& pos, const std::optional<uint256>& expected_hash)
{
    return m_blockman.ReadBlock(block, pos, expected_hash);
}

bool CoreBlockDataStore::ReadBlockUndo(CBlockUndo& blockundo, const CBlockIndex& index)
{
    return m_blockman.ReadBlockUndo(blockundo, index);
}

Consensus::BlockCommitResult<void> CoreBlockDataStore::WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    if (const auto undo_write{m_blockman.WriteBlockUndo(blockundo, index)}; !undo_write) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{Consensus::BlockCommitError{
            .reject_reason = undo_write.error().reject_reason,
        }};
    }
    return {};
}

bool CoreBlockDataStore::IsPruneMode() const
{
    return m_blockman.IsPruneMode();
}

bool CoreBlockDataStore::HasIndexedBlockFiles() const
{
    return m_blockman.m_blockfiles_indexed;
}

FlatFilePos CoreBlockDataStore::WriteBlock(const CBlock& block, int height)
{
    return m_blockman.WriteBlock(block, height);
}

void CoreBlockDataStore::UpdateBlockInfo(const CBlock& block, unsigned int height, const FlatFilePos& pos)
{
    m_blockman.UpdateBlockInfo(block, height, pos);
}
