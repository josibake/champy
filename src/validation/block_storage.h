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
#include <util/expected.h>

#include <optional>

class CBlockIndex;

struct BlockDataReadRequest {
    FlatFilePos position;
    uint256 expected_hash;
    int height{-1};
};

struct BlockUndoReadRequest {
    FlatFilePos position;
    uint256 block_hash;
    uint256 previous_block_hash;
    int height{-1};
};

enum class BlockDataReadError {
    NotIndexed,
    DataUnavailable,
    Pruned,
    MalformedStoredData,
    IoError,
    Interrupted,
};

using BlockDataReadResult = util::Expected<CBlock, BlockDataReadError>;

enum class BlockUndoReadError {
    GenesisHasNoUndo,
    NotIndexed,
    UndoUnavailable,
    Pruned,
    MalformedStoredData,
    IoError,
    Interrupted,
};

using BlockUndoReadResult = util::Expected<CBlockUndo, BlockUndoReadError>;

class BlockDataReader
{
public:
    virtual ~BlockDataReader() = default;

    virtual BlockDataReadResult ReadBlock(const BlockDataReadRequest& request) = 0;
    virtual BlockDataReadResult ReadBlockFromPosition(const FlatFilePos& pos, const std::optional<uint256>& expected_hash) = 0;
};

class BlockUndoReader
{
public:
    virtual ~BlockUndoReader() = default;

    virtual BlockUndoReadResult ReadBlockUndo(const BlockUndoReadRequest& request) = 0;
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
