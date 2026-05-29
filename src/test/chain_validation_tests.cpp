// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <validation/chain_validation.h>
#include <chainparams.h>
#include <chainstate.h>
#include <test/util/mining.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <validation_state.h>

BOOST_FIXTURE_TEST_SUITE(chain_validation_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(service_accepts_headers)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const NewBlockHeadersResult result{validation.ProcessNewBlockHeaders(
        {{*block}},
        {.min_pow_checked = true},
        CurrentBlockValidationTime(),
        state)};

    BOOST_CHECK(state.IsValid());
    BOOST_REQUIRE(result.accepted);
    BOOST_REQUIRE(result.last_accepted);
    BOOST_CHECK(result.last_accepted->GetBlockHash() == block->GetHash());
}

BOOST_AUTO_TEST_CASE(service_accepts_requested_block_data)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    LOCK(cs_main);
    const BlockAcceptanceResult result{validation.AcceptBlock(
        block,
        state,
        {.block_data_requested = true, .header = {.min_pow_checked = true}},
        CurrentBlockValidationTime())};

    BOOST_CHECK(state.IsValid());
    BOOST_REQUIRE(result.accepted_for_processing());
    BOOST_REQUIRE(result.stored_block_data());
    BOOST_REQUIRE(result.block_index);
    BOOST_CHECK(result.block_index->GetBlockHash() == block->GetHash());
}

BOOST_AUTO_TEST_CASE(service_processes_new_block)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    const NewBlockProcessingResult result{validation.ProcessNewBlock(
        block,
        {.force_processing = true, .header = {.min_pow_checked = true}},
        CurrentBlockValidationTime())};

    BOOST_REQUIRE(result.processed());
    BOOST_REQUIRE(result.new_block());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(service_test_block_validity_uses_explicit_time)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    LOCK(cs_main);
    const BlockValidationState state{validation.TestBlockValidity(
        chainman.ActiveChainstate(),
        *block,
        {},
        {.current_time_seconds = 0, .max_future_block_time = static_cast<int64_t>(block->nTime) - 1})};

    BOOST_CHECK(!state.IsValid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "time-too-new");
}

BOOST_AUTO_TEST_SUITE_END()
