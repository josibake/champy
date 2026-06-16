// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_BLOCK_DOWNLOAD_PLANNER_H
#define BITCOIN_NODE_BLOCK_DOWNLOAD_PLANNER_H

#include <node/block_download_types.h>
#include <node/ibd_block_download.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace node {

[[nodiscard]] std::optional<uint256> PeerBlockRefHash(const std::optional<PeerBlockRef>& block);

struct BlockDownloadCandidateFacts {
    PeerBlockRef block{};
    bool valid_tree{false};
    bool segwit_active{false};
    bool has_data{false};
    bool in_active_chain{false};
    bool have_num_chain_txs{false};
};

struct BlockDownloadChainFacts {
    bool found{false};
    bool interesting{false};
    int window_end{-1};
    int best_known_height{-1};
    std::optional<PeerBlockRef> last_common{};
    std::span<const BlockDownloadCandidateFacts> candidates{};
};

struct BlockDownloadPlannerRequest {
    int64_t peer{-1};
    unsigned int max_blocks{0};
    bool limited_peer{false};
    bool can_serve_witnesses{false};
    int limited_peer_min_blocks{0};
    BlockDownloadChainFacts chain{};
    std::span<const BlockInFlight> blocks_in_flight{};
    std::optional<IbdBlockDownloadWindow> ibd_window{};
};

struct BlockDownloadPlannerResult {
    std::vector<PeerBlockRef> blocks{};
    std::optional<PeerBlockRef> last_common{};
    std::optional<int64_t> staller{};
};

[[nodiscard]] BlockDownloadPlannerResult PlanNextBlocksToDownload(const BlockDownloadPlannerRequest& request);

} // namespace node

#endif // BITCOIN_NODE_BLOCK_DOWNLOAD_PLANNER_H
