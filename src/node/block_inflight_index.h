// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_BLOCK_INFLIGHT_INDEX_H
#define BITCOIN_NODE_BLOCK_INFLIGHT_INDEX_H

#include <node/block_download_types.h>
#include <uint256.h>

#include <cstdint>
#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace node {

class BlockInFlightIndex {
public:
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] size_t size() const noexcept { return m_size; }

    [[nodiscard]] bool Contains(const uint256& hash) const;
    [[nodiscard]] bool Contains(const uint256& hash, int64_t peer) const;
    [[nodiscard]] size_t Count(const uint256& hash) const;
    [[nodiscard]] std::optional<int64_t> FirstPeer(const uint256& hash) const;
    [[nodiscard]] std::vector<int64_t> Peers(const uint256& hash) const;
    [[nodiscard]] std::vector<uint256> Hashes() const;
    [[nodiscard]] std::vector<BlockInFlight> Snapshot() const;
    [[nodiscard]] bool AllEntriesMatch(const uint256& hash) const;

    void Add(const uint256& hash, int64_t peer);
    bool Erase(const uint256& hash, int64_t peer);

private:
    std::map<uint256, std::vector<int64_t>> m_by_block{};
    size_t m_size{0};
};

} // namespace node

#endif // BITCOIN_NODE_BLOCK_INFLIGHT_INDEX_H
