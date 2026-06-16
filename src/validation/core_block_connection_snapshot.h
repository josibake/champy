// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SNAPSHOT_H
#define BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SNAPSHOT_H

#include <kernel/cs_main.h>
#include <validation/snapshot_block_connection_state.h>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;

namespace validation {

[[nodiscard]] SnapshotBlockConnectionState SnapshotCoreBlockConnectionState(
    const CBlock& block,
    const CBlockIndex& block_index,
    const CCoinsViewCache& coins) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

class CoreCoinsBlockConnectionSnapshotter final {
public:
    explicit CoreCoinsBlockConnectionSnapshotter(const CCoinsViewCache& coins) : m_coins{coins} {}

    [[nodiscard]] SnapshotBlockConnectionState Snapshot(const CBlock& block, const CBlockIndex& block_index) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    const CCoinsViewCache& m_coins;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_CORE_BLOCK_CONNECTION_SNAPSHOT_H
