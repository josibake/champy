// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <chainstate.h>
#include <node/ibd_block_processor.h>
#include <node/ibd_validated_block.h>
#include <test/util/mining.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/chain_validation.h>
#include <validation/core_block_commit_adapters.h>
#include <validation/core_block_connection_snapshot.h>
#include <validation/core_chain_activation.h>
#include <validation/core_chain_validation_context.h>
#include <validation/core_coins_block_connection_state.h>
#include <validation_state.h>

#include <chrono>
#include <optional>
#include <vector>

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
    BOOST_REQUIRE(validation.ActivateAcceptedBlock(/*chain_events=*/nullptr, block, activation_state).Succeeded());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(service_activates_accepted_tip_candidate)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{validation.AcceptNewBlockData(
        block,
        state,
        {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        CurrentBlockValidationTime())};
    BOOST_REQUIRE(accepted.accepted_for_processing());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 0);

    BlockValidationState activation_state;
    const BlockActivationResult activated{validation.ActivateAcceptedTipCandidate(/*chain_events=*/nullptr, block, activation_state)};
    BOOST_REQUIRE(activated.Succeeded());
    BOOST_CHECK_EQUAL(activated.connected_blocks, 1U);
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);

    BlockValidationState stale_state;
    const BlockActivationResult stale{validation.ActivateAcceptedTipCandidate(/*chain_events=*/nullptr, block, stale_state)};
    BOOST_REQUIRE(stale.Succeeded());
    BOOST_CHECK_EQUAL(stale.connected_blocks, 0U);
}

BOOST_AUTO_TEST_CASE(core_connect_tip_can_run_as_explicit_stages)
{
    auto& chainman{*Assert(m_node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    ChainValidationService chain_validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{chain_validation.AcceptNewBlockData(
        block,
        state,
        {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        CurrentBlockValidationTime())};
    BOOST_REQUIRE(accepted.accepted_for_processing());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 0);

    LOCK(cs_main);
    CoreChainValidationRuntime runtime{chainman};
    CoreChainValidationContext context{chainman, runtime};
    CoreBlockDataStore block_store{chainman.m_blockman};
    CoreBlockIndexStore block_index_store{chainman};
    validation::CoreCoinsBlockConnectionState connection_state{chainstate.CoinsTip()};
    validation::CoreCoinsBlockConnectionSnapshotter connection_snapshotter{chainstate.CoinsTip()};
    CoreBlockSpendEffectsCommitter spend_state_committer{chainstate.CoinsTip()};
    std::vector<ConnectedBlock> connected_blocks;
    BlockActivationTimings activation_timings;
    uint64_t activation_connected_blocks{0};
    CBlockIndex* block_index{Assert(block_index_store.LookupBlockIndex(block->GetHash()))};

    auto prepared{PrepareCoreConnectTip(
        {
            .resources = {
                .context = context,
                .block_reader = block_store,
                .block_index_lookup = block_index_store,
                .connection_snapshotter = connection_snapshotter,
                .last_script_check_reason_logged = chainstate.LastScriptCheckReasonLogged(),
                .chain_lock = nullptr,
            },
            .block_index = *block_index,
            .cached_block = block,
        },
        state)};
    BOOST_REQUIRE(prepared);
    BOOST_CHECK(prepared->block == block);

    auto executed{ExecutePreparedCoreConnectTip({.context = context, .chain_lock = nullptr}, std::move(*prepared), state)};
    ReportCoreConnectTipExecution(
        {
            .context = context,
            .block_index_lookup = block_index_store,
            .validation_events = runtime.ValidationEvents(),
            .time_connect_total = chainman.TimeConnectTotal(),
            .blocks_total = chainman.NumBlocksTotal(),
        },
        executed,
        state);
    BOOST_REQUIRE(executed.execution);
    BOOST_CHECK(executed.execution->block_position.hash == block->GetHash());
    BOOST_CHECK(executed.execution->block_position.parent_hash == Params().GenesisBlock().GetHash());
    BOOST_CHECK_EQUAL(executed.execution->block_position.height, 1);
    BOOST_CHECK(executed.execution->commit_package.expected_previous_block == Params().GenesisBlock().GetHash());

    node::IbdOrderedPackageRetireQueue<node::IbdValidatedTipPackage> retire_queue{node::IbdRetireChainPosition{
        .next_height = 1,
        .expected_parent_hash = Params().GenesisBlock().GetHash(),
    }};
    const node::IbdRetireResult retire_result{retire_queue.Add(node::MakeIbdValidatedTipPackage(std::move(*executed.execution)))};
    BOOST_CHECK(retire_result.status == node::IbdRetireStatus::Ready);

    std::vector<node::IbdValidatedTipPackage> ready{retire_queue.PopReady()};
    BOOST_REQUIRE_EQUAL(ready.size(), 1U);

    const CoreConnectTipResult committed{CommitCoreConnectTip(
        {
            .context = context,
            .undo_writer = block_store,
            .block_index_lookup = block_index_store,
            .block_index_committer = block_index_store,
            .connection_state = connection_state,
            .spend_state_committer = spend_state_committer,
            .connected_blocks = connected_blocks,
            .chain_events = nullptr,
            .timing = {
                .time_connect_total = chainman.TimeConnectTotal(),
                .time_flush = chainman.TimeFlush(),
                .time_chainstate = chainman.TimeChainstate(),
                .time_post_connect = chainman.TimePostConnect(),
                .time_total = chainman.TimeTotal(),
                .blocks_total = chainman.NumBlocksTotal(),
            },
            .activation_timings = activation_timings,
            .activation_connected_blocks = activation_connected_blocks,
        },
        std::move(ready[0].execution),
        state)};
    BOOST_REQUIRE(committed.Succeeded());
    BOOST_CHECK_EQUAL(activation_connected_blocks, 1U);
    BOOST_CHECK_EQUAL(chainstate.m_chain.Height(), 1);
    BOOST_REQUIRE_EQUAL(connected_blocks.size(), 1U);
    BOOST_CHECK(connected_blocks.front().pindex == block_index);
}

BOOST_AUTO_TEST_CASE(ibd_block_processor_records_stage_metrics)
{
    auto& chainman{*Assert(m_node.chainman)};
    node::IbdBlockProcessor processor{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    {
        LOCK(cs_main);
        const node::IbdPipelineAdmissionWindow window{
            processor.AdmissionWindow(node::IbdPipelineLimits{.max_blocks_ahead = 7})};
        BOOST_CHECK_EQUAL(window.next_commit_height, 1);
        BOOST_REQUIRE(window.expected_parent_hash);
        BOOST_CHECK(*window.expected_parent_hash == Params().GenesisBlock().GetHash());
        BOOST_CHECK_EQUAL(window.limits.max_blocks_ahead, 7U);
    }

    const node::IbdBlockProcessResult result{processor.ProcessDownloadedBlock({
        .block = block,
        .block_data_storage = BlockDataStorageMode::ForceStore,
        .min_pow_checked = true,
        .time = CurrentBlockValidationTime(),
    })};

    BOOST_REQUIRE(result.validation.processed());
    BOOST_REQUIRE(result.validation.new_block());
    BOOST_CHECK_EQUAL(result.validation.activated_blocks, 1U);
    BOOST_REQUIRE(result.accepted_candidate);
    BOOST_CHECK(result.accepted_candidate->block.hash == block->GetHash());
    BOOST_CHECK(result.accepted_candidate->block.parent_hash == Params().GenesisBlock().GetHash());
    BOOST_CHECK_EQUAL(result.accepted_candidate->block.height, 1);
    BOOST_CHECK(result.accepted_candidate->block_data == block);

    const node::IbdPipelineMetrics& metrics{processor.Metrics()};
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::Download).blocks, 1U);
    BOOST_CHECK_GT(metrics.Stage(node::IbdPipelineStage::Download).bytes, 0U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::StructuralValidation).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::BlockAdmission).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::ContextSnapshot).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::SpendJoin).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::ScriptValidation).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::Commit).blocks, 1U);

    {
        LOCK(cs_main);
        const node::IbdPipelineAdmissionWindow window{
            processor.AdmissionWindow(node::IbdPipelineLimits{.max_blocks_ahead = 7})};
        BOOST_CHECK_EQUAL(window.next_commit_height, 2);
        BOOST_REQUIRE(window.expected_parent_hash);
        BOOST_CHECK(*window.expected_parent_hash == block->GetHash());
    }
}

BOOST_AUTO_TEST_CASE(ibd_candidate_builds_segment_view_and_commit_package)
{
    auto& chainman{*Assert(m_node.chainman)};
    ChainValidationService validation{chainman};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{validation.AcceptNewBlockData(
        block,
        state,
        {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        CurrentBlockValidationTime())};
    BOOST_REQUIRE(accepted.accepted_for_processing());

    const std::optional<NewBlockCandidateContextSnapshot> snapshot{validation.SnapshotAcceptedBlockContext(block->GetHash())};
    BOOST_REQUIRE(snapshot);
    node::IbdAcceptedBlockCandidate candidate{
        .block = {
            .hash = snapshot->block.hash,
            .parent_hash = snapshot->block.parent_hash,
            .height = snapshot->block.height,
            .chain_work = snapshot->block.chain_work,
        },
        .block_data = block,
        .context = *snapshot,
    };

    const Consensus::SegmentBlockView segment_view{node::BuildIbdSegmentBlockView(candidate)};
    BOOST_CHECK(segment_view.context.hash == block->GetHash());
    BOOST_CHECK(segment_view.context.parent_hash == Params().GenesisBlock().GetHash());
    BOOST_CHECK_EQUAL(segment_view.context.height, 1);
    BOOST_CHECK_EQUAL(segment_view.context.block_subsidy, snapshot->block_subsidy);
    BOOST_CHECK_EQUAL(segment_view.transactions.size(), block->vtx.size());

    Consensus::BlockSpendEffects effects;
    effects.fees = 7;
    node::IbdValidatedBlockPackage package{node::MakeIbdValidatedBlockPackage(
        std::move(candidate),
        effects,
        node::IbdScriptValidationStatus::Valid)};

    BOOST_CHECK(package.ReadyForSerializedCommit());
    BOOST_CHECK(package.block.hash == block->GetHash());
    BOOST_CHECK(package.parent_hash == Params().GenesisBlock().GetHash());
    BOOST_REQUIRE(package.commit_work);
    BOOST_CHECK(package.commit_work->block_data == block);
    BOOST_CHECK(package.commit_work->commit_context.new_best_block == block->GetHash());
    BOOST_CHECK_EQUAL(package.commit_work->commit_context.block_height, 1);
    BOOST_CHECK_EQUAL(package.commit_work->spend_effects.fees, 7);
}

BOOST_AUTO_TEST_CASE(ibd_block_processor_records_only_reached_stages)
{
    auto& chainman{*Assert(m_node.chainman)};
    node::IbdBlockProcessor processor{chainman};
    const auto valid_block{CreateBlockChain(/*total_height=*/1, Params()).front()};
    auto invalid_block{std::make_shared<CBlock>(*valid_block)};
    invalid_block->hashMerkleRoot = valid_block->hashMerkleRoot == uint256::ONE ? uint256::ZERO : uint256::ONE;

    const node::IbdBlockProcessResult result{processor.ProcessDownloadedBlock({
        .block = invalid_block,
        .block_data_storage = BlockDataStorageMode::ForceStore,
        .min_pow_checked = true,
        .time = CurrentBlockValidationTime(),
    })};

    BOOST_CHECK(result.validation.status == NewBlockProcessingStatus::BlockCheckFailed);
    BOOST_CHECK(!result.accepted_candidate);

    const node::IbdPipelineMetrics& metrics{processor.Metrics()};
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::Download).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::StructuralValidation).blocks, 1U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::BlockAdmission).blocks, 0U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::ContextSnapshot).blocks, 0U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::SpendJoin).blocks, 0U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::ScriptValidation).blocks, 0U);
    BOOST_CHECK_EQUAL(metrics.Stage(node::IbdPipelineStage::Commit).blocks, 0U);
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
