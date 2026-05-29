// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_DATA_ADAPTERS_H
#define BITCOIN_BLOCK_DATA_ADAPTERS_H

#include <flatfile.h>
#include <primitives/block.h>
#include <uint256.h>
#include <undo.h>

#include <optional>

class CBlockIndex;

namespace node {
class BlockManager;
} // namespace node

class BlockDataStore
{
public:
    virtual ~BlockDataStore() = default;

    virtual bool ReadBlock(CBlock& block, const CBlockIndex& index) = 0;
    virtual bool ReadBlockFromPosition(CBlock& block, const FlatFilePos& pos, const std::optional<uint256>& expected_hash) = 0;
    virtual bool ReadBlockUndo(CBlockUndo& blockundo, const CBlockIndex& index) = 0;
    virtual FlatFilePos WriteBlock(const CBlock& block, int height) = 0;
    virtual void UpdateBlockInfo(const CBlock& block, unsigned int height, const FlatFilePos& pos) = 0;
};

class CoreBlockDataStore final : public BlockDataStore
{
public:
    explicit CoreBlockDataStore(node::BlockManager& blockman) : m_blockman{blockman} {}

    bool ReadBlock(CBlock& block, const CBlockIndex& index) override;
    bool ReadBlockFromPosition(CBlock& block, const FlatFilePos& pos, const std::optional<uint256>& expected_hash) override;
    bool ReadBlockUndo(CBlockUndo& blockundo, const CBlockIndex& index) override;
    FlatFilePos WriteBlock(const CBlock& block, int height) override;
    void UpdateBlockInfo(const CBlock& block, unsigned int height, const FlatFilePos& pos) override;

private:
    node::BlockManager& m_blockman;
};

#endif // BITCOIN_BLOCK_DATA_ADAPTERS_H
