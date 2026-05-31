// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/block_inflight_index.h>

#include <algorithm>

namespace node {

bool BlockInFlightIndex::Contains(const uint256& hash) const
{
    return m_by_block.contains(hash);
}

bool BlockInFlightIndex::Contains(const uint256& hash, int64_t peer) const
{
    const auto it{m_by_block.find(hash)};
    if (it == m_by_block.end()) return false;
    return std::ranges::find(it->second, peer) != it->second.end();
}

size_t BlockInFlightIndex::Count(const uint256& hash) const
{
    const auto it{m_by_block.find(hash)};
    if (it == m_by_block.end()) return 0;
    return it->second.size();
}

std::optional<int64_t> BlockInFlightIndex::FirstPeer(const uint256& hash) const
{
    const auto it{m_by_block.find(hash)};
    if (it == m_by_block.end() || it->second.empty()) return std::nullopt;
    return it->second.front();
}

std::vector<int64_t> BlockInFlightIndex::Peers(const uint256& hash) const
{
    const auto it{m_by_block.find(hash)};
    if (it == m_by_block.end()) return {};
    return it->second;
}

std::vector<uint256> BlockInFlightIndex::Hashes() const
{
    std::vector<uint256> hashes;
    hashes.reserve(m_size);
    for (const auto& [hash, peers] : m_by_block) {
        hashes.insert(hashes.end(), peers.size(), hash);
    }
    return hashes;
}

std::vector<BlockInFlight> BlockInFlightIndex::Snapshot() const
{
    std::vector<BlockInFlight> blocks;
    blocks.reserve(m_size);
    for (const auto& [hash, peers] : m_by_block) {
        for (const int64_t peer : peers) {
            blocks.push_back({.hash = hash, .peer = peer});
        }
    }
    return blocks;
}

bool BlockInFlightIndex::AllEntriesMatch(const uint256& hash) const
{
    if (m_size == 0) return false;
    const auto it{m_by_block.find(hash)};
    return it != m_by_block.end() && it->second.size() == m_size;
}

void BlockInFlightIndex::Add(const uint256& hash, int64_t peer)
{
    auto& peers{m_by_block[hash]};
    if (std::ranges::find(peers, peer) != peers.end()) return;
    peers.push_back(peer);
    ++m_size;
}

bool BlockInFlightIndex::Erase(const uint256& hash, int64_t peer)
{
    const auto it{m_by_block.find(hash)};
    if (it == m_by_block.end()) return false;

    auto& peers{it->second};
    const auto peer_it{std::ranges::find(peers, peer)};
    if (peer_it == peers.end()) return false;

    peers.erase(peer_it);
    --m_size;
    if (peers.empty()) {
        m_by_block.erase(it);
    }
    return true;
}

} // namespace node
