// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <block_index_adapters.h>

#include <chainstate.h>
#include <flatfile.h>
#include <node/blockstorage.h>

const CBlockIndex* CoreBlockIndexView::LookupBlockIndex(const uint256& block_hash) const
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return m_chainman.m_blockman.LookupBlockIndex(block_hash);
}

std::vector<const CBlockIndex*> CoreBlockIndexView::SnapshotBlockIndices() const
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return m_chainman.m_blockman.GetAllBlockIndices();
}

CBlockIndex* CoreBlockIndexStore::LookupBlockIndex(const uint256& block_hash)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return m_chainman.m_blockman.LookupBlockIndex(block_hash);
}

std::vector<CBlockIndex*> CoreBlockIndexStore::SnapshotBlockIndices()
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return m_chainman.m_blockman.GetAllBlockIndices();
}

void CoreBlockIndexStore::MarkBlockIndexDirty(CBlockIndex& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    m_chainman.m_blockman.DirtyBlockIndex().insert(&block_index);
}

void CoreBlockIndexStore::MarkBlockDataReceived(const CBlock& block, CBlockIndex& block_index, const FlatFilePos& pos)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    m_chainman.ReceivedBlockTransactions(block, &block_index, pos);
}

CBlockIndex* CoreBlockIndexStore::AddToBlockIndex(const CBlockHeader& block)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return m_chainman.m_blockman.AddToBlockIndex(block, m_chainman.m_best_header);
}
