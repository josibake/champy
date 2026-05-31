// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <node/block_download_tracker.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

using namespace std::chrono_literals;

namespace {

uint256 Hash(int value)
{
    return ArithToUint256(arith_uint256{static_cast<uint64_t>(value)});
}

node::PeerBlockRef BlockRef(int value, int height)
{
    return node::PeerBlockRef{.hash = Hash(value), .height = height};
}

} // namespace

BOOST_AUTO_TEST_SUITE(block_download_tracker_tests)

BOOST_AUTO_TEST_CASE(requests_update_peer_queue_and_global_index_together)
{
    node::BlockDownloadTracker tracker;
    const auto block{BlockRef(1, 101)};

    const auto requested{tracker.RequestBlock(/*peer=*/7, block, /*use_compact_block=*/false, /*mempool=*/nullptr, 1us)};

    BOOST_CHECK(requested.added);
    BOOST_REQUIRE(requested.queued_block != nullptr);
    BOOST_CHECK_EQUAL(requested.queued_block->block.hash.ToString(), block.hash.ToString());
    BOOST_CHECK(!tracker.empty());
    BOOST_CHECK_EQUAL(tracker.size(), 1);
    BOOST_CHECK_EQUAL(tracker.Count(block.hash), 1);
    BOOST_CHECK(tracker.Contains(block.hash, 7));
    BOOST_CHECK_EQUAL(tracker.PeerInFlightCount(7), 1);
    BOOST_CHECK_EQUAL(tracker.PeersDownloadingFrom(), 1);
}

BOOST_AUTO_TEST_CASE(duplicate_request_returns_existing_queue_entry)
{
    node::BlockDownloadTracker tracker;
    const auto block{BlockRef(1, 101)};

    const auto first{tracker.RequestBlock(7, block, false, nullptr, 1us)};
    const auto duplicate{tracker.RequestBlock(7, block, false, nullptr, 2us)};

    BOOST_CHECK(!duplicate.added);
    BOOST_CHECK_EQUAL(duplicate.queued_block, first.queued_block);
    BOOST_CHECK_EQUAL(tracker.size(), 1);
    BOOST_CHECK_EQUAL(tracker.PeerInFlightCount(7), 1);
    BOOST_CHECK_EQUAL(tracker.PeersDownloadingFrom(), 1);
}

BOOST_AUTO_TEST_CASE(removing_first_queued_block_preserves_next_download_state)
{
    node::BlockDownloadTracker tracker;
    const auto first{BlockRef(1, 101)};
    const auto second{BlockRef(2, 102)};

    tracker.RequestBlock(7, first, false, nullptr, 1us);
    tracker.RequestBlock(7, second, false, nullptr, 2us);
    tracker.MarkStallingIfUnset(7, 10us);

    tracker.RemoveBlockRequest(first.hash, std::nullopt, 3us);

    BOOST_CHECK(!tracker.Contains(first.hash));
    BOOST_CHECK(tracker.Contains(second.hash, 7));
    BOOST_CHECK_EQUAL(tracker.PeerInFlightCount(7), 1);
    BOOST_CHECK_EQUAL(tracker.PeersDownloadingFrom(), 1);
    BOOST_CHECK_EQUAL(tracker.DownloadingSince(7).count(), 3);
    BOOST_CHECK_EQUAL(tracker.StallingSince(7).count(), 0);

    const auto queued{tracker.FirstInFlightBlock(7)};
    BOOST_REQUIRE(queued.has_value());
    BOOST_CHECK_EQUAL(queued->hash.ToString(), second.hash.ToString());
}

BOOST_AUTO_TEST_CASE(removing_last_queued_block_stops_peer_download)
{
    node::BlockDownloadTracker tracker;
    const auto block{BlockRef(1, 101)};

    tracker.RequestBlock(7, block, false, nullptr, 1us);
    tracker.RemoveBlockRequest(block.hash, 7, 2us);

    BOOST_CHECK(tracker.empty());
    BOOST_CHECK_EQUAL(tracker.PeerInFlightCount(7), 0);
    BOOST_CHECK_EQUAL(tracker.PeersDownloadingFrom(), 0);
}

BOOST_AUTO_TEST_CASE(forgetting_peer_removes_peer_from_global_index)
{
    node::BlockDownloadTracker tracker;
    const auto first{BlockRef(1, 101)};
    const auto second{BlockRef(2, 102)};

    tracker.RequestBlock(7, first, false, nullptr, 1us);
    tracker.RequestBlock(11, second, false, nullptr, 1us);
    tracker.ForgetPeer(7);

    BOOST_CHECK(!tracker.Contains(first.hash));
    BOOST_CHECK(tracker.Contains(second.hash, 11));
    BOOST_CHECK_EQUAL(tracker.PeerInFlightCount(7), 0);
    BOOST_CHECK_EQUAL(tracker.PeerInFlightCount(11), 1);
    BOOST_CHECK_EQUAL(tracker.PeersDownloadingFrom(), 1);

    tracker.ForgetPeer(11);
    BOOST_CHECK(tracker.empty());
    BOOST_CHECK(tracker.PeerStatesEmpty());
    BOOST_CHECK_EQUAL(tracker.PeersDownloadingFrom(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
