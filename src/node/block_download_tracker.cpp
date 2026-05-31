// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/block_download_tracker.h>

#include <blockencodings.h>

#include <algorithm>
#include <cassert>
#include <utility>

namespace node {

QueuedBlock::QueuedBlock(PeerBlockRef block_in, std::unique_ptr<PartiallyDownloadedBlock> partial_block) :
    block{std::move(block_in)},
    partialBlock{std::move(partial_block)}
{
}

QueuedBlock::~QueuedBlock() = default;
QueuedBlock::QueuedBlock(QueuedBlock&&) noexcept = default;
QueuedBlock& QueuedBlock::operator=(QueuedBlock&&) noexcept = default;

bool BlockDownloadTracker::Contains(const uint256& hash) const
{
    return m_blocks_in_flight.Contains(hash);
}

bool BlockDownloadTracker::Contains(const uint256& hash, int64_t peer) const
{
    return m_blocks_in_flight.Contains(hash, peer);
}

size_t BlockDownloadTracker::Count(const uint256& hash) const
{
    return m_blocks_in_flight.Count(hash);
}

std::optional<int64_t> BlockDownloadTracker::FirstPeer(const uint256& hash) const
{
    return m_blocks_in_flight.FirstPeer(hash);
}

std::vector<int64_t> BlockDownloadTracker::Peers(const uint256& hash) const
{
    return m_blocks_in_flight.Peers(hash);
}

std::vector<uint256> BlockDownloadTracker::Hashes() const
{
    return m_blocks_in_flight.Hashes();
}

std::vector<BlockInFlight> BlockDownloadTracker::Snapshot() const
{
    return m_blocks_in_flight.Snapshot();
}

bool BlockDownloadTracker::AllEntriesMatch(const uint256& hash) const
{
    return m_blocks_in_flight.AllEntriesMatch(hash);
}

bool BlockDownloadTracker::PeerHasInFlight(int64_t peer) const
{
    return PeerInFlightCount(peer) > 0;
}

size_t BlockDownloadTracker::PeerInFlightCount(int64_t peer) const
{
    const PeerBlockDownloadState* state{FindState(peer)};
    return state ? state->blocks_in_flight.size() : 0;
}

std::vector<int> BlockDownloadTracker::PeerInFlightHeights(int64_t peer) const
{
    const PeerBlockDownloadState* state{FindState(peer)};
    if (state == nullptr) return {};

    std::vector<int> heights;
    heights.reserve(state->blocks_in_flight.size());
    for (const QueuedBlock& queued_block : state->blocks_in_flight) {
        heights.push_back(queued_block.block.height);
    }
    return heights;
}

std::optional<PeerBlockRef> BlockDownloadTracker::FirstInFlightBlock(int64_t peer) const
{
    const PeerBlockDownloadState* state{FindState(peer)};
    if (state == nullptr || state->blocks_in_flight.empty()) return std::nullopt;
    return state->blocks_in_flight.front().block;
}

std::chrono::microseconds BlockDownloadTracker::DownloadingSince(int64_t peer) const
{
    const PeerBlockDownloadState* state{FindState(peer)};
    return state ? state->downloading_since : std::chrono::microseconds{0};
}

std::chrono::microseconds BlockDownloadTracker::StallingSince(int64_t peer) const
{
    const PeerBlockDownloadState* state{FindState(peer)};
    return state ? state->stalling_since : std::chrono::microseconds{0};
}

QueuedBlock* BlockDownloadTracker::QueuedBlockFor(int64_t peer, const uint256& hash)
{
    PeerBlockDownloadState* state{FindState(peer)};
    if (state == nullptr) return nullptr;
    return FindQueuedBlockPtr(*state, hash);
}

BlockDownloadTracker::RequestResult BlockDownloadTracker::RequestBlock(int64_t peer,
                                                                        const PeerBlockRef& block,
                                                                        bool use_compact_block,
                                                                        CTxMemPool* mempool,
                                                                        std::chrono::microseconds now)
{
    PeerBlockDownloadState& state{StateFor(peer)};
    if (m_blocks_in_flight.Contains(block.hash, peer)) {
        return {.added = false, .queued_block = FindQueuedBlockPtr(state, block.hash)};
    }

    RemoveBlockRequest(block.hash, peer, now);

    std::unique_ptr<PartiallyDownloadedBlock> partial_block;
    if (use_compact_block) {
        assert(mempool != nullptr);
        partial_block = std::make_unique<PartiallyDownloadedBlock>(mempool);
    }
    const auto queued_block{state.blocks_in_flight.insert(
        state.blocks_in_flight.end(),
        QueuedBlock{block, std::move(partial_block)})};

    if (state.blocks_in_flight.size() == 1) {
        state.downloading_since = now;
        ++m_peers_downloading_from;
    }
    m_blocks_in_flight.Add(block.hash, peer);
    return {.added = true, .queued_block = &*queued_block};
}

void BlockDownloadTracker::RemoveBlockRequest(const uint256& hash,
                                              std::optional<int64_t> from_peer,
                                              std::chrono::microseconds now)
{
    const std::vector<int64_t> peers{m_blocks_in_flight.Peers(hash)};
    for (const int64_t peer : peers) {
        if (from_peer && *from_peer != peer) continue;

        PeerBlockDownloadState* state{FindState(peer)};
        if (state == nullptr) {
            m_blocks_in_flight.Erase(hash, peer);
            continue;
        }

        const auto queued_block{FindQueuedBlock(*state, hash)};
        if (queued_block == state->blocks_in_flight.end()) {
            m_blocks_in_flight.Erase(hash, peer);
            continue;
        }

        if (state->blocks_in_flight.begin() == queued_block) {
            state->downloading_since = std::max(state->downloading_since, now);
        }
        state->blocks_in_flight.erase(queued_block);

        if (state->blocks_in_flight.empty()) {
            --m_peers_downloading_from;
        }
        state->stalling_since = std::chrono::microseconds{0};
        m_blocks_in_flight.Erase(hash, peer);
    }
}

void BlockDownloadTracker::ForgetPeer(int64_t peer)
{
    const auto state_it{m_peer_download.find(peer)};
    if (state_it == m_peer_download.end()) return;

    for (const QueuedBlock& queued_block : state_it->second.blocks_in_flight) {
        m_blocks_in_flight.Erase(queued_block.block.hash, peer);
    }
    if (!state_it->second.blocks_in_flight.empty()) {
        --m_peers_downloading_from;
    }
    m_peer_download.erase(state_it);
}

void BlockDownloadTracker::MarkStallingIfUnset(int64_t peer, std::chrono::microseconds now)
{
    PeerBlockDownloadState& state{StateFor(peer)};
    if (state.stalling_since == std::chrono::microseconds{0}) {
        state.stalling_since = now;
    }
}

BlockDownloadTracker::PeerBlockDownloadState& BlockDownloadTracker::StateFor(int64_t peer)
{
    return m_peer_download.try_emplace(peer).first->second;
}

const BlockDownloadTracker::PeerBlockDownloadState* BlockDownloadTracker::FindState(int64_t peer) const
{
    const auto it{m_peer_download.find(peer)};
    if (it == m_peer_download.end()) return nullptr;
    return &it->second;
}

BlockDownloadTracker::PeerBlockDownloadState* BlockDownloadTracker::FindState(int64_t peer)
{
    const auto it{m_peer_download.find(peer)};
    if (it == m_peer_download.end()) return nullptr;
    return &it->second;
}

std::list<QueuedBlock>::iterator BlockDownloadTracker::FindQueuedBlock(PeerBlockDownloadState& state, const uint256& hash)
{
    return std::ranges::find_if(state.blocks_in_flight, [&](const QueuedBlock& entry) {
        return entry.block.hash == hash;
    });
}

QueuedBlock* BlockDownloadTracker::FindQueuedBlockPtr(PeerBlockDownloadState& state, const uint256& hash)
{
    const auto it{FindQueuedBlock(state, hash)};
    if (it == state.blocks_in_flight.end()) return nullptr;
    return &*it;
}

} // namespace node
