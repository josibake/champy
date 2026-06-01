// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <chainstate.h>
#include <node/ibd_block_processor.h>
#include <test/util/mining.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <validation/chain_validation.h>
#include <validation_state.h>

#include <chrono>
#include <optional>

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
    BOOST_CHECK(result.last_accepted->block.hash == block->GetHash());
    BOOST_CHECK_EQUAL(result.last_accepted->block.height, 1);
    BOOST_CHECK_EQUAL(result.last_accepted->block_time, block->GetBlockTime());
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
        {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        CurrentBlockValidationTime())};

    BOOST_CHECK(state.IsValid());
    BOOST_REQUIRE(result.accepted_for_processing());
    BOOST_REQUIRE(result.stored_block_data());
    BOOST_REQUIRE(result.block);
    BOOST_CHECK(result.block->hash == block->GetHash());
    BOOST_CHECK_EQUAL(result.block->height, 1);
}

BOOST_AUTO_TEST_CASE(service_processes_new_block)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    const NewBlockProcessingResult result{validation.ProcessNewBlock(
        block,
        {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        CurrentBlockValidationTime())};

    BOOST_REQUIRE(result.processed());
    BOOST_REQUIRE(result.new_block());
    BOOST_REQUIRE(result.candidate_context);
    BOOST_CHECK(result.candidate_context->Matches(*block));
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(service_processes_new_block_with_structural_proof)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const NewBlockStructuralCheckResult structural{validation.CheckNewBlockStructural(block, state)};
    BOOST_REQUIRE(structural.passed());

    const NewBlockProcessingResult result{validation.ProcessNewBlock(
        block,
        {
            .block_data_storage = BlockDataStorageMode::ForceStore,
            .header = {.min_pow_checked = true},
            .structural_check = structural.proof,
        },
        CurrentBlockValidationTime())};

    BOOST_REQUIRE(result.processed());
    BOOST_REQUIRE(result.new_block());
    BOOST_REQUIRE(result.candidate_context);
    BOOST_CHECK(result.candidate_context->Matches(*block));
    BOOST_CHECK(result.timings.structural_check == std::chrono::nanoseconds{0});
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(service_exposes_structural_accept_and_activate_stages)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};
    const BlockValidationTime time{CurrentBlockValidationTime()};

    BlockValidationState state;
    const NewBlockStructuralCheckResult structural{validation.CheckNewBlockStructural(block, state)};
    BOOST_REQUIRE(structural.passed());
    BOOST_REQUIRE(structural.proof);
    BOOST_CHECK(structural.proof->Matches(*block));

    const BlockAcceptanceResult accepted{validation.AcceptNewBlockData(
        block,
        state,
        {
            .block_data_storage = BlockDataStorageMode::ForceStore,
            .header = {.min_pow_checked = true},
            .structural_check = structural.proof,
        },
        time)};
    BOOST_REQUIRE(accepted.accepted_for_processing());
    BOOST_REQUIRE(accepted.stored_block_data());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 0);

    const std::optional<NewBlockCandidateContextSnapshot> snapshot{validation.SnapshotAcceptedBlockContext(block->GetHash())};
    BOOST_REQUIRE(snapshot);
    BOOST_CHECK(snapshot->Matches(*block));
    BOOST_CHECK(snapshot->block.hash == block->GetHash());
    BOOST_CHECK_EQUAL(snapshot->block.height, 1);
    BOOST_CHECK(snapshot->previous_block_hash == Params().GenesisBlock().GetHash());
    BOOST_CHECK_EQUAL(snapshot->previous_block_height, 0);
    BOOST_CHECK(snapshot->has_spend_stage);
    BOOST_CHECK_EQUAL(snapshot->block_subsidy, GetBlockSubsidy(/*nHeight=*/1, Params().GetConsensus()));

    BlockValidationState activation_state;
    BOOST_REQUIRE(validation.ActivateAcceptedBlock(/*chain_events=*/nullptr, block, activation_state));
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(ibd_block_processor_records_stage_metrics)
{
    auto& chainman{*Assert(m_node.chainman)};
    node::IbdBlockProcessor processor{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    const node::IbdBlockProcessResult result{processor.ProcessDownloadedBlock({
        .block = block,
        .block_data_storage = BlockDataStorageMode::ForceStore,
        .min_pow_checked = true,
        .time = CurrentBlockValidationTime(),
    })};

    BOOST_REQUIRE(result.validation.processed());
    BOOST_REQUIRE(result.validation.new_block());

    const node::IbdPipelineMetrics& metrics{processor.Metrics()};
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::Download).blocks, 1U);
    BOOST_CHECK_GT(metrics.Stage(node::IbdPipelineStage::Download).bytes, 0U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::StructuralValidation).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::BlockAdmission).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::ContextSnapshot).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::Commit).blocks, 1U);
}

BOOST_AUTO_TEST_CASE(service_test_block_validity_uses_explicit_time)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    const BlockValidationState state{validation.TestActiveBlockValidity(
        *block,
        {},
        {.current_time_seconds = 0, .max_future_block_time = static_cast<int64_t>(block->nTime) - 1})};

    BOOST_CHECK(!state.IsValid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "time-too-new");
}

BOOST_AUTO_TEST_SUITE_END()
