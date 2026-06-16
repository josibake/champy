// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <node/block_inflight_index.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

namespace {

uint256 Hash(int value)
{
    return ArithToUint256(arith_uint256{static_cast<uint64_t>(value)});
}

} // namespace

BOOST_AUTO_TEST_SUITE(block_inflight_index_tests)

BOOST_AUTO_TEST_CASE(index_tracks_membership_and_counts)
{
    node::BlockInFlightIndex index;
    const uint256 block{Hash(1)};

    BOOST_CHECK(index.empty());
    BOOST_CHECK_EQUAL(index.size(), 0);
    BOOST_CHECK(!index.Contains(block));

    index.Add(block, 7);
    index.Add(block, 11);

    BOOST_CHECK(!index.empty());
    BOOST_CHECK_EQUAL(index.size(), 2);
    BOOST_CHECK_EQUAL(index.Count(block), 2);
    BOOST_CHECK(index.Contains(block));
    BOOST_CHECK(index.Contains(block, 7));
    BOOST_CHECK(index.Contains(block, 11));
    BOOST_CHECK(!index.Contains(block, 13));
}

BOOST_AUTO_TEST_CASE(index_preserves_first_peer_for_same_block)
{
    node::BlockInFlightIndex index;
    const uint256 block{Hash(1)};

    index.Add(block, 7);
    index.Add(block, 11);

    BOOST_REQUIRE(index.FirstPeer(block));
    BOOST_CHECK_EQUAL(*index.FirstPeer(block), 7);

    BOOST_REQUIRE(index.Erase(block, 7));
    BOOST_REQUIRE(index.FirstPeer(block));
    BOOST_CHECK_EQUAL(*index.FirstPeer(block), 11);
}

BOOST_AUTO_TEST_CASE(index_snapshots_download_facts)
{
    node::BlockInFlightIndex index;
    const uint256 first{Hash(1)};
    const uint256 second{Hash(2)};

    index.Add(first, 7);
    index.Add(second, 11);

    const auto hashes{index.Hashes()};
    BOOST_REQUIRE_EQUAL(hashes.size(), 2);
    BOOST_CHECK_EQUAL(hashes[0].ToString(), first.ToString());
    BOOST_CHECK_EQUAL(hashes[1].ToString(), second.ToString());

    const auto snapshot{index.Snapshot()};
    BOOST_REQUIRE_EQUAL(snapshot.size(), 2);
    BOOST_CHECK_EQUAL(snapshot[0].hash.ToString(), first.ToString());
    BOOST_CHECK_EQUAL(snapshot[0].peer, 7);
    BOOST_CHECK_EQUAL(snapshot[1].hash.ToString(), second.ToString());
    BOOST_CHECK_EQUAL(snapshot[1].peer, 11);
}

BOOST_AUTO_TEST_CASE(index_rejects_duplicate_peer_entries)
{
    node::BlockInFlightIndex index;
    const uint256 block{Hash(1)};

    index.Add(block, 7);
    index.Add(block, 7);

    BOOST_CHECK_EQUAL(index.size(), 1);
    BOOST_CHECK_EQUAL(index.Count(block), 1);
}

BOOST_AUTO_TEST_CASE(index_can_check_single_block_coverage)
{
    node::BlockInFlightIndex index;
    const uint256 block{Hash(1)};
    const uint256 other{Hash(2)};

    BOOST_CHECK(!index.AllEntriesMatch(block));

    index.Add(block, 7);
    index.Add(block, 11);
    BOOST_CHECK(index.AllEntriesMatch(block));

    index.Add(other, 13);
    BOOST_CHECK(!index.AllEntriesMatch(block));
}

BOOST_AUTO_TEST_SUITE_END()
