// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_INDEX_H
#define BITCOIN_VALIDATION_BLOCK_INDEX_H

#include <kernel/cs_main.h>
#include <primitives/block.h>
#include <uint256.h>

#include <vector>

class CBlockIndex;
struct FlatFilePos;

class BlockIndexView
{
public:
    virtual ~BlockIndexView() = default;

    virtual const CBlockIndex* LookupBlockIndex(const uint256& block_hash) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    //! Return block-index entries present at snapshot time. Entries are owned by the underlying block index.
    virtual std::vector<const CBlockIndex*> SnapshotBlockIndices() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class BlockIndexLookup
{
public:
    virtual ~BlockIndexLookup() = default;

    virtual CBlockIndex* LookupBlockIndex(const uint256& block_hash) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class BlockIndexValidityCommitter
{
public:
    virtual ~BlockIndexValidityCommitter() = default;

    virtual void MarkBlockIndexDirty(CBlockIndex& block_index) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class BlockIndexHeaderStore : public BlockIndexLookup
{
public:
    ~BlockIndexHeaderStore() override = default;

    virtual CBlockIndex* AddToBlockIndex(const CBlockHeader& block) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class BlockIndexDataReceiver
{
public:
    virtual ~BlockIndexDataReceiver() = default;

    virtual void MarkBlockDataReceived(const CBlock& block, CBlockIndex& block_index, const FlatFilePos& pos) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

#endif // BITCOIN_VALIDATION_BLOCK_INDEX_H
