// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_REPLAY_H
#define BITCOIN_VALIDATION_BLOCK_REPLAY_H

#include <coins.h>
#include <kernel/cs_main.h>
#include <validation/block_storage.h>
#include <uint256.h>

#include <optional>
#include <vector>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class BlockDataReader;
class BlockIndexLookup;
class BlockUndoReader;
class CCoinsView;

namespace kernel {
class Notifications;
} // namespace kernel

/**
 * Dependencies needed to repair an interrupted coins DB flush.
 *
 * Replay uses Core's current block-index entries, but it does not need a broad
 * Chainstate object. Keeping the storage, index, and notification capabilities
 * explicit makes replay testable and keeps the recovery algorithm reusable for
 * alternate coins/state backends.
 */
struct BlockReplayBlock {
    uint256 hash;
    uint256 previous_hash;
    int height{-1};
    BlockDataReadRequest block_read;
    std::optional<BlockUndoReadRequest> undo_read;
};

[[nodiscard]] BlockReplayBlock SnapshotCoreBlockReplayBlock(const CBlockIndex& block_index) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

class BlockReplayIndex
{
public:
    virtual ~BlockReplayIndex() = default;

    [[nodiscard]] virtual std::optional<BlockReplayBlock> LookupBlock(const uint256& block_hash) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual std::optional<BlockReplayBlock> Previous(const BlockReplayBlock& block) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual std::optional<BlockReplayBlock> AncestorAtHeight(const BlockReplayBlock& block, int height) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual std::optional<BlockReplayBlock> LastCommonAncestor(const BlockReplayBlock& a, const BlockReplayBlock& b) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class CoreBlockReplayIndex final : public BlockReplayIndex
{
public:
    explicit CoreBlockReplayIndex(BlockIndexLookup& block_index);

    [[nodiscard]] std::optional<BlockReplayBlock> LookupBlock(const uint256& block_hash) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] std::optional<BlockReplayBlock> Previous(const BlockReplayBlock& block) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] std::optional<BlockReplayBlock> AncestorAtHeight(const BlockReplayBlock& block, int height) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] std::optional<BlockReplayBlock> LastCommonAncestor(const BlockReplayBlock& a, const BlockReplayBlock& b) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    BlockIndexLookup& m_block_index;
};

class BlockReplayCoins
{
public:
    virtual ~BlockReplayCoins() = default;

    [[nodiscard]] virtual std::vector<uint256> GetHeadBlocks() const = 0;
    [[nodiscard]] virtual DisconnectResult DisconnectBlock(BlockUndoReader& undo_reader, const CBlock& block, const BlockReplayBlock& block_index) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual bool RollforwardBlock(const CBlock& block, const BlockReplayBlock& block_index) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    virtual void SetBestBlock(const uint256& block_hash) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    virtual void Flush() EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class CoreBlockReplayCoins final : public BlockReplayCoins
{
public:
    explicit CoreBlockReplayCoins(CCoinsView& coins_db);

    [[nodiscard]] std::vector<uint256> GetHeadBlocks() const override;
    [[nodiscard]] DisconnectResult DisconnectBlock(BlockUndoReader& undo_reader, const CBlock& block, const BlockReplayBlock& block_index) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] bool RollforwardBlock(const CBlock& block, const BlockReplayBlock& block_index) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void SetBestBlock(const uint256& block_hash) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void Flush() override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    CCoinsViewCache m_cache;
};

struct BlockReplayRequest {
    BlockReplayCoins& coins;
    BlockDataReader& block_reader;
    BlockUndoReader& undo_reader;
    BlockReplayIndex& block_index;
    kernel::Notifications& notifications;
};

DisconnectResult DisconnectBlock(BlockUndoReader& undo_reader, const CBlock& block, const BlockReplayBlock& block_index, CCoinsViewCache& view) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool RollforwardBlock(const CBlock& block, const BlockReplayBlock& block_index, CCoinsViewCache& inputs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
bool ReplayBlocks(const BlockReplayRequest& request) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_VALIDATION_BLOCK_REPLAY_H
