// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/block_download_planner.h>

#include <algorithm>

namespace node {

std::optional<uint256> PeerBlockRefHash(const std::optional<PeerBlockRef>& block)
{
    if (!block) return std::nullopt;
    return block->hash;
}

static std::optional<int64_t> FindInFlightPeer(std::span<const BlockInFlight> blocks_in_flight, const uint256& hash)
{
    const auto it{std::ranges::find(blocks_in_flight, hash, &BlockInFlight::hash)};
    if (it == blocks_in_flight.end()) return std::nullopt;
    return it->peer;
}

static bool IbdPipelineAccepts(const std::optional<IbdPipelineAdmissionWindow>& window, const PeerBlockRef& block)
{
    if (!window) return true;
    const IbdPipeline pipeline{
        IbdRetireChainPosition{
            .next_height = window->next_commit_height,
            .expected_parent_hash = window->expected_parent_hash,
        },
        window->limits};
    return pipeline.Admit(block).status == IbdAdmissionStatus::Accepted;
}

BlockDownloadPlannerResult PlanNextBlocksToDownload(const BlockDownloadPlannerRequest& request)
{
    BlockDownloadPlannerResult result;
    if (request.max_blocks == 0) return result;
    if (!request.chain.found || !request.chain.interesting) return result;

    result.last_common = request.chain.last_common;

    std::optional<int64_t> waiting_for;
    for (const BlockDownloadCandidateFacts& candidate : request.chain.candidates) {
        if (!candidate.valid_tree) {
            return result;
        }

        if (!request.can_serve_witnesses && candidate.segwit_active) {
            return result;
        }

        if (candidate.has_data || candidate.in_active_chain) {
            if (candidate.have_num_chain_txs) {
                result.last_common = candidate.block;
            }
            continue;
        }

        if (const auto in_flight_peer{FindInFlightPeer(request.blocks_in_flight, candidate.block.hash)}) {
            if (!waiting_for) {
                waiting_for = in_flight_peer;
            }
            continue;
        }

        if (candidate.block.height > request.chain.window_end) {
            if (result.blocks.empty() && waiting_for && *waiting_for != request.peer) {
                result.staller = *waiting_for;
            }
            return result;
        }

        if (!IbdPipelineAccepts(request.ibd_pipeline, candidate.block)) {
            return result;
        }

        if (request.limited_peer &&
            request.chain.best_known_height - candidate.block.height >= request.limited_peer_min_blocks - 2) {
            continue;
        }

        result.blocks.push_back(candidate.block);
        if (result.blocks.size() == request.max_blocks) {
            return result;
        }
    }
    return result;
}

} // namespace node
