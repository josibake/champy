// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_BLOCK_DOWNLOAD_TRACKER_H
#define BITCOIN_NODE_BLOCK_DOWNLOAD_TRACKER_H

#include <node/block_download_types.h>
#include <node/block_inflight_index.h>
#include <uint256.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class CTxMemPool;
class PartiallyDownloadedBlock;

namespace node {

/** Blocks queued for download from one peer, in request order. */
struct QueuedBlock {
    QueuedBlock(PeerBlockRef block_in, std::unique_ptr<PartiallyDownloadedBlock> partial_block);
    ~QueuedBlock();
    QueuedBlock(QueuedBlock&&) noexcept;
    QueuedBlock& operator=(QueuedBlock&&) noexcept;
    QueuedBlock(const QueuedBlock&) = delete;
    QueuedBlock& operator=(const QueuedBlock&) = delete;

    /** Block metadata. We only request blocks after validating their header. */
    PeerBlockRef block;
    /** Optional compact-block reconstruction state. */
    std::unique_ptr<PartiallyDownloadedBlock> partialBlock;
};

/** Tracks block-download queues and the global in-flight index together.
 *
 * This class is not internally synchronized. Callers own the lock that protects it.
 */
class BlockDownloadTracker {
public:
    struct RequestResult {
        bool added{false};
        /** Valid until this tracker mutates the same peer queue or forgets the peer. */
        QueuedBlock* queued_block{nullptr};
    };

    [[nodiscard]] bool empty() const noexcept { return m_blocks_in_flight.empty(); }
    [[nodiscard]] size_t size() const noexcept { return m_blocks_in_flight.size(); }
    [[nodiscard]] bool PeerStatesEmpty() const noexcept { return m_peer_download.empty(); }
    [[nodiscard]] int PeersDownloadingFrom() const noexcept { return m_peers_downloading_from; }

    [[nodiscard]] bool Contains(const uint256& hash) const;
    [[nodiscard]] bool Contains(const uint256& hash, int64_t peer) const;
    [[nodiscard]] size_t Count(const uint256& hash) const;
    [[nodiscard]] std::optional<int64_t> FirstPeer(const uint256& hash) const;
    [[nodiscard]] std::vector<int64_t> Peers(const uint256& hash) const;
    [[nodiscard]] std::vector<uint256> Hashes() const;
    [[nodiscard]] std::vector<BlockInFlight> Snapshot() const;
    [[nodiscard]] bool AllEntriesMatch(const uint256& hash) const;

    [[nodiscard]] bool PeerHasInFlight(int64_t peer) const;
    [[nodiscard]] size_t PeerInFlightCount(int64_t peer) const;
    [[nodiscard]] std::vector<int> PeerInFlightHeights(int64_t peer) const;
    [[nodiscard]] std::optional<PeerBlockRef> FirstInFlightBlock(int64_t peer) const;
    [[nodiscard]] std::chrono::microseconds DownloadingSince(int64_t peer) const;
    [[nodiscard]] std::chrono::microseconds StallingSince(int64_t peer) const;
    [[nodiscard]] QueuedBlock* QueuedBlockFor(int64_t peer, const uint256& hash);

    RequestResult RequestBlock(int64_t peer,
                               const PeerBlockRef& block,
                               bool use_compact_block,
                               CTxMemPool* mempool,
                               std::chrono::microseconds now);
    void RemoveBlockRequest(const uint256& hash,
                            std::optional<int64_t> from_peer,
                            std::chrono::microseconds now);
    void ForgetPeer(int64_t peer);
    void MarkStallingIfUnset(int64_t peer, std::chrono::microseconds now);

private:
    struct PeerBlockDownloadState {
        //! Since when this peer is stalling block download progress, or 0.
        std::chrono::microseconds stalling_since{std::chrono::microseconds{0}};
        //! Blocks that are in flight from this peer, in request order.
        std::list<QueuedBlock> blocks_in_flight{};
        //! When the first in-flight block started downloading. Only meaningful when blocks_in_flight is not empty.
        std::chrono::microseconds downloading_since{std::chrono::microseconds{0}};
    };

    PeerBlockDownloadState& StateFor(int64_t peer);
    [[nodiscard]] const PeerBlockDownloadState* FindState(int64_t peer) const;
    [[nodiscard]] PeerBlockDownloadState* FindState(int64_t peer);
    [[nodiscard]] static std::list<QueuedBlock>::iterator FindQueuedBlock(PeerBlockDownloadState& state, const uint256& hash);
    [[nodiscard]] static QueuedBlock* FindQueuedBlockPtr(PeerBlockDownloadState& state, const uint256& hash);

    std::map<int64_t, PeerBlockDownloadState> m_peer_download{};
    BlockInFlightIndex m_blocks_in_flight{};
    int m_peers_downloading_from{0};
};

} // namespace node

#endif // BITCOIN_NODE_BLOCK_DOWNLOAD_TRACKER_H
