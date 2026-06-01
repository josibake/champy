// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/block_download_planner.h>

#include <boost/test/unit_test.hpp>

namespace {

node::PeerBlockRef Block(int height)
{
    const arith_uint256 value{static_cast<uint64_t>(height)};
    return {
        .hash = ArithToUint256(value),
        .height = height,
        .chain_work = value,
    };
}

node::BlockDownloadCandidateFacts Candidate(int height)
{
    return {
        .block = Block(height),
        .valid_tree = true,
    };
}

node::BlockDownloadChainFacts Chain(std::span<const node::BlockDownloadCandidateFacts> candidates)
{
    return {
        .found = true,
        .interesting = true,
        .window_end = 103,
        .best_known_height = 105,
        .last_common = Block(100),
        .candidates = candidates,
    };
}

} // namespace

BOOST_AUTO_TEST_SUITE(block_download_planner_tests)

BOOST_AUTO_TEST_CASE(planner_requests_missing_blocks_in_order)
{
    const std::vector candidates{Candidate(101), Candidate(102), Candidate(103)};

    const auto plan{node::PlanNextBlocksToDownload({
        .peer = 7,
        .max_blocks = 2,
        .limited_peer_min_blocks = 288,
        .chain = Chain(candidates),
    })};

    BOOST_REQUIRE_EQUAL(plan.blocks.size(), 2);
    BOOST_CHECK_EQUAL(plan.blocks[0].hash.ToString(), Block(101).hash.ToString());
    BOOST_CHECK_EQUAL(plan.blocks[1].hash.ToString(), Block(102).hash.ToString());
    BOOST_REQUIRE(plan.last_common);
    BOOST_CHECK_EQUAL(plan.last_common->height, 100);
    BOOST_CHECK(!plan.staller);
}

BOOST_AUTO_TEST_CASE(planner_updates_last_common_from_available_chain_data)
{
    std::vector candidates{Candidate(101), Candidate(102), Candidate(103)};
    candidates[0].has_data = true;
    candidates[0].have_num_chain_txs = true;
    candidates[1].in_active_chain = true;
    candidates[1].have_num_chain_txs = true;

    const auto plan{node::PlanNextBlocksToDownload({
        .peer = 7,
        .max_blocks = 4,
        .limited_peer_min_blocks = 288,
        .chain = Chain(candidates),
    })};

    BOOST_REQUIRE(plan.last_common);
    BOOST_CHECK_EQUAL(plan.last_common->height, 102);
    BOOST_REQUIRE_EQUAL(plan.blocks.size(), 1);
    BOOST_CHECK_EQUAL(plan.blocks.front().height, 103);
}

BOOST_AUTO_TEST_CASE(planner_stops_on_invalid_or_unservable_chain)
{
    std::vector candidates{Candidate(101), Candidate(102)};
    candidates[0].valid_tree = false;

    auto plan{node::PlanNextBlocksToDownload({
        .peer = 7,
        .max_blocks = 4,
        .limited_peer_min_blocks = 288,
        .chain = Chain(candidates),
    })};
    BOOST_CHECK(plan.blocks.empty());

    candidates[0] = Candidate(101);
    candidates[0].segwit_active = true;
    plan = node::PlanNextBlocksToDownload({
        .peer = 7,
        .max_blocks = 4,
        .can_serve_witnesses = false,
        .limited_peer_min_blocks = 288,
        .chain = Chain(candidates),
    });
    BOOST_CHECK(plan.blocks.empty());
}

BOOST_AUTO_TEST_CASE(planner_marks_staller_at_download_window)
{
    const std::vector candidates{Candidate(101), Candidate(102), Candidate(104)};
    const std::vector in_flight{
        node::BlockInFlight{.hash = Block(101).hash, .peer = 42},
        node::BlockInFlight{.hash = Block(102).hash, .peer = 42},
    };

    const auto plan{node::PlanNextBlocksToDownload({
        .peer = 7,
        .max_blocks = 4,
        .limited_peer_min_blocks = 288,
        .chain = Chain(candidates),
        .blocks_in_flight = in_flight,
    })};

    BOOST_CHECK(plan.blocks.empty());
    BOOST_REQUIRE(plan.staller);
    BOOST_CHECK_EQUAL(*plan.staller, 42);
}

BOOST_AUTO_TEST_CASE(planner_respects_pruned_peer_service_window)
{
    const std::vector candidates{Candidate(101), Candidate(102), Candidate(103)};

    const auto plan{node::PlanNextBlocksToDownload({
        .peer = 7,
        .max_blocks = 4,
        .limited_peer = true,
        .can_serve_witnesses = true,
        .limited_peer_min_blocks = 6,
        .chain = Chain(candidates),
    })};

    BOOST_REQUIRE_EQUAL(plan.blocks.size(), 2);
    BOOST_CHECK_EQUAL(plan.blocks[0].height, 102);
    BOOST_CHECK_EQUAL(plan.blocks[1].height, 103);
}

BOOST_AUTO_TEST_CASE(planner_respects_ibd_pipeline_admission_window)
{
    const std::vector candidates{Candidate(101), Candidate(102), Candidate(103), Candidate(104)};

    const auto plan{node::PlanNextBlocksToDownload({
        .peer = 7,
        .max_blocks = 4,
        .can_serve_witnesses = true,
        .limited_peer_min_blocks = 288,
        .chain = Chain(candidates),
        .ibd_pipeline = node::IbdPipelineAdmissionWindow{
            .next_commit_height = 101,
            .limits = node::IbdPipelineLimits{.max_blocks_ahead = 2},
        },
    })};

    BOOST_REQUIRE_EQUAL(plan.blocks.size(), 2);
    BOOST_CHECK_EQUAL(plan.blocks[0].height, 101);
    BOOST_CHECK_EQUAL(plan.blocks[1].height, 102);
}

BOOST_AUTO_TEST_SUITE_END()
