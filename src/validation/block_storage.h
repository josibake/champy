// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_STORAGE_H
#define BITCOIN_VALIDATION_BLOCK_STORAGE_H

#include <consensus/block_commit.h>
#include <flatfile.h>
#include <kernel/cs_main.h>
#include <primitives/block.h>
#include <uint256.h>
#include <undo.h>

#include <optional>

class CBlockIndex;

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

class BlockDataAvailability
{
public:
    virtual ~BlockDataAvailability() = default;

    virtual bool IsPruneMode() const = 0;
};

#endif // BITCOIN_VALIDATION_BLOCK_STORAGE_H
