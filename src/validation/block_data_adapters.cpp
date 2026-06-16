// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_data_adapters.h>

#include <chain.h>
#include <kernel/blockstorage.h>
#include <util/check.h>

#include <utility>

BlockDataReadResult CoreBlockDataStore::ReadBlock(const BlockDataReadRequest& request)
{
    CBlock block;
    if (!m_blockman.ReadBlock(block, request.position, request.expected_hash)) {
        return util::Unexpected{BlockDataReadError::IoError};
    }
    return std::move(block);
}

BlockDataReadResult CoreBlockDataStore::ReadBlockFromPosition(const FlatFilePos& pos, const std::optional<uint256>& expected_hash)
{
    CBlock block;
    if (!m_blockman.ReadBlock(block, pos, expected_hash)) {
        return util::Unexpected{BlockDataReadError::IoError};
    }
    return std::move(block);
}

BlockUndoReadResult CoreBlockDataStore::ReadBlockUndo(const BlockUndoReadRequest& request)
{
    if (request.height == 0) {
        return util::Unexpected{BlockUndoReadError::GenesisHasNoUndo};
    }

    CBlockUndo blockundo;
    if (!m_blockman.ReadBlockUndo(blockundo, request.position, request.previous_block_hash)) {
        return util::Unexpected{BlockUndoReadError::IoError};
    }
    return std::move(blockundo);
}

Consensus::BlockCommitResult<void> CoreBlockDataStore::WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    if (const auto undo_write{m_blockman.WriteBlockUndo(blockundo, index)}; !undo_write) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{Consensus::BlockCommitError{
            .runtime_issue = Consensus::ValidationRuntimeIssue::SystemError,
            .failure_state = Consensus::BlockCommitFailureState::Tainted,
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

BlockDataReadRequest SnapshotBlockDataReadRequest(const CBlockIndex& index)
{
    AssertLockHeld(::cs_main);
    return {
        .position = index.GetBlockPos(),
        .expected_hash = index.GetBlockHash(),
        .height = index.nHeight,
    };
}

BlockUndoReadRequest SnapshotBlockUndoReadRequest(const CBlockIndex& index)
{
    AssertLockHeld(::cs_main);
    return {
        .position = index.GetUndoPos(),
        .block_hash = index.GetBlockHash(),
        .previous_block_hash = Assert(index.pprev)->GetBlockHash(),
        .height = index.nHeight,
    };
}
