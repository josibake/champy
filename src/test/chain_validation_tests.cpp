// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <chainstate.h>
#include <consensus/block_commit.h>
#include <consensus/expected.h>
#include <consensus/consensus.h>
#include <node/kernel_notifications.h>
#include <test/util/mining.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/chain_validation.h>
#include <validation/block_connection_state.h>
#include <validation/core_block_commit_adapters.h>
#include <validation/core_block_connection_snapshot.h>
#include <validation/core_chain_activation.h>
#include <validation/core_chain_validation_runtimes.h>
#include <validation/core_coins_block_connection_state.h>
#include <validation/runtime_time.h>
#include <validationinterface.h>
#include <validation_state.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

class BlockCheckedRecorder final : public CValidationInterface
{
public:
    int checked_count{0};
    std::optional<uint256> checked_hash;
    bool checked_valid{false};

private:
    void BlockChecked(const validation::BlockCheckedEvent& event) override
    {
        ++checked_count;
        checked_hash = event.block->GetHash();
        checked_valid = event.state.IsValid();
    }
};

class TaintedSpendCommitter final : public Consensus::SpendCommitter
{
public:
    Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Consensus::Unexpected<Consensus::BlockCommitError>{Consensus::BlockCommitError{
            .failure_state = Consensus::BlockCommitFailureState::Tainted,
            .reject_reason = "forced-tainted-commit",
        }};
    }
};

class ThrowingCommitGuard final : public validation::BlockConnectionAttemptGuard
{
public:
    void Commit() override { throw std::runtime_error{"flush failed"}; }
};

class ThrowingCommitConnectionState final : public validation::BlockConnectionState
{
public:
    explicit ThrowingCommitConnectionState(uint256 best_block) : m_best_block{std::move(best_block)} {}

    [[nodiscard]] uint256 BestBlock() const override { return m_best_block; }
    void SetBestBlock(const uint256& block_hash) override { m_best_block = block_hash; }
    [[nodiscard]] std::unique_ptr<validation::BlockConnectionAttemptGuard> BeginConnectionAttempt() override
    {
        return std::make_unique<ThrowingCommitGuard>();
    }
    [[nodiscard]] Consensus::BlockSpendResult<std::unique_ptr<validation::BlockConnectionSpendState>> BeginBlockSpend(
        const Consensus::BlockSpendContext&,
        std::shared_ptr<const Consensus::SequenceLockTimeView>) override
    {
        return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
            .issue = Consensus::BlockConsensusIssue::ValidationRuntime,
            .runtime_issue = Consensus::ValidationRuntimeIssue::SystemError,
            .reject_reason = "unexpected-spend-begin",
            .debug_message = "",
        }};
    }

private:
    uint256 m_best_block;
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(chain_validation_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(service_accepts_headers)
{
    auto& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const NewBlockHeadersResult result{ProcessNewBlockHeaders({
        .chainman = chainman,
        .headers = {{*block}},
        .options = {.min_pow_checked = true},
        .time = CurrentBlockValidationTime(),
        .state = state,
    })};

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
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    LOCK(cs_main);
    const BlockAcceptanceResult result{AcceptBlock({
        .chainman = chainman,
        .block = block,
        .state = state,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = CurrentBlockValidationTime(),
    })};

    BOOST_CHECK(state.IsValid());
    BOOST_REQUIRE(result.ShouldAttemptActivation());
    BOOST_REQUIRE(result.HasStoredBlockData());
    BOOST_REQUIRE(result.block());
    BOOST_CHECK(result.block()->hash == block->GetHash());
    BOOST_CHECK_EQUAL(result.block()->height, 1);
}

BOOST_AUTO_TEST_CASE(service_processes_new_block)
{
    auto& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    const NewBlockProcessingResult result{ProcessNewBlock({
        .chainman = chainman,
        .block = block,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = CurrentBlockValidationTime(),
    })};

    BOOST_REQUIRE(result.Processed());
    BOOST_REQUIRE(result.HasNewStoredBlockData());
    BOOST_REQUIRE(result.candidate_context());
    BOOST_CHECK(result.candidate_context()->Matches(*block));
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(service_processes_new_block_with_structural_proof)
{
    auto& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const NewBlockStructuralCheckResult structural{CheckNewBlockStructural({
        .chainman = chainman,
        .block = block,
        .state = state,
    })};
    BOOST_REQUIRE(structural.passed());

    const NewBlockProcessingResult result{ProcessNewBlock({
        .chainman = chainman,
        .block = block,
        .options = {
            .block_data_storage = BlockDataStorageMode::ForceStore,
            .header = {.min_pow_checked = true},
            .structural_check = structural.proof,
        },
        .time = CurrentBlockValidationTime(),
    })};

    BOOST_REQUIRE(result.Processed());
    BOOST_REQUIRE(result.HasNewStoredBlockData());
    BOOST_REQUIRE(result.candidate_context());
    BOOST_CHECK(result.candidate_context()->Matches(*block));
    BOOST_CHECK(result.timings.structural_check == std::chrono::nanoseconds{0});
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(service_exposes_structural_accept_and_activate_stages)
{
    auto& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};
    const BlockValidationTime time{CurrentBlockValidationTime()};

    BlockValidationState state;
    const NewBlockStructuralCheckResult structural{CheckNewBlockStructural({
        .chainman = chainman,
        .block = block,
        .state = state,
    })};
    BOOST_REQUIRE(structural.passed());
    BOOST_REQUIRE(structural.proof);
    BOOST_CHECK(structural.proof->Matches(*block));

    const BlockAcceptanceResult accepted{AcceptNewBlockData({
        .chainman = chainman,
        .block = block,
        .state = state,
        .options = {
            .block_data_storage = BlockDataStorageMode::ForceStore,
            .header = {.min_pow_checked = true},
            .structural_check = structural.proof,
        },
        .time = time,
    })};
    BOOST_REQUIRE(accepted.ShouldAttemptActivation());
    BOOST_REQUIRE(accepted.HasStoredBlockData());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 0);

    const std::optional<NewBlockCandidateContextSnapshot> snapshot{SnapshotAcceptedBlockContext({
        .chainman = chainman,
        .block_hash = block->GetHash(),
    })};
    BOOST_REQUIRE(snapshot);
    BOOST_CHECK(snapshot->Matches(*block));
    BOOST_CHECK(snapshot->block.hash == block->GetHash());
    BOOST_CHECK_EQUAL(snapshot->block.height, 1);
    BOOST_CHECK(snapshot->previous_block_hash == Params().GenesisBlock().GetHash());
    BOOST_CHECK_EQUAL(snapshot->previous_block_height, 0);
    BOOST_CHECK(snapshot->has_spend_stage);
    BOOST_CHECK_EQUAL(snapshot->block_subsidy, GetBlockSubsidy(/*nHeight=*/1, Params().GetConsensus()));

    BlockValidationState activation_state;
    BOOST_REQUIRE(ActivateAcceptedBlock({
        .chainman = chainman,
        .chain_events = nullptr,
        .block = block,
        .state = activation_state,
        .time = CurrentBlockValidationTime(),
    }).Succeeded());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);
}

BOOST_AUTO_TEST_CASE(service_activates_accepted_tip_candidate)
{
    auto& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{AcceptNewBlockData({
        .chainman = chainman,
        .block = block,
        .state = state,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = CurrentBlockValidationTime(),
    })};
    BOOST_REQUIRE(accepted.ShouldAttemptActivation());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 0);

    BlockValidationState activation_state;
    const BlockActivationResult activated{ActivateAcceptedTipCandidate({
        .chainman = chainman,
        .chain_events = nullptr,
        .block = block,
        .state = activation_state,
        .time = CurrentBlockValidationTime(),
    })};
    BOOST_REQUIRE(activated.Succeeded());
    BOOST_CHECK_EQUAL(activated.connected_blocks, 1U);
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 1);

    BlockValidationState stale_state;
    const BlockActivationResult stale{ActivateAcceptedTipCandidate({
        .chainman = chainman,
        .chain_events = nullptr,
        .block = block,
        .state = stale_state,
        .time = CurrentBlockValidationTime(),
    })};
    BOOST_REQUIRE(stale.Succeeded());
    BOOST_CHECK_EQUAL(stale.connected_blocks, 0U);
}

BOOST_AUTO_TEST_CASE(core_connect_tip_can_run_as_explicit_stages)
{
    auto& chainman{*Assert(m_node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{AcceptNewBlockData({
        .chainman = chainman,
        .block = block,
        .state = state,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = CurrentBlockValidationTime(),
    })};
    BOOST_REQUIRE(accepted.ShouldAttemptActivation());
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()), 0);

    LOCK(cs_main);
    CoreActivationRuntime runtime{chainman};
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
                .runtime = runtime,
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

    auto executed{ExecutePreparedCoreConnectTip({.runtime = runtime, .chain_lock = nullptr}, std::move(*prepared), state)};
    ReportCoreConnectTipExecution(
        {
            .runtime = runtime,
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

    const CoreConnectTipResult committed{CommitCoreBlockConnection(
        {
            .runtime = runtime,
            .undo_writer = block_store,
            .block_index_lookup = block_index_store,
            .block_index_committer = block_index_store,
            .connection_state = connection_state,
            .spend_state_committer = spend_state_committer,
            .connected_blocks = connected_blocks,
            .chain_events = nullptr,
            .current_time = CurrentNodeTime(),
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
        std::move(*executed.execution), state)};
    BOOST_REQUIRE(committed.Succeeded());
    BOOST_CHECK_EQUAL(activation_connected_blocks, 1U);
    BOOST_CHECK_EQUAL(chainstate.m_chain.Height(), 1);
    BOOST_REQUIRE_EQUAL(connected_blocks.size(), 1U);
    BOOST_CHECK(connected_blocks.front().block_info.hash == block_index->GetBlockHash());
    BOOST_CHECK_EQUAL(connected_blocks.front().block_info.height, block_index->nHeight);
}

BOOST_AUTO_TEST_CASE(core_connect_tip_tainted_commit_failure_is_fatal_before_tip_reuse)
{
    auto& chainman{*Assert(m_node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{AcceptNewBlockData({
        .chainman = chainman,
        .block = block,
        .state = state,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = CurrentBlockValidationTime(),
    })};
    BOOST_REQUIRE(accepted.ShouldAttemptActivation());

    LOCK(cs_main);
    CoreActivationRuntime runtime{chainman};
    CoreBlockDataStore block_store{chainman.m_blockman};
    CoreBlockIndexStore block_index_store{chainman};
    validation::CoreCoinsBlockConnectionState connection_state{chainstate.CoinsTip()};
    TaintedSpendCommitter spend_state_committer;
    std::vector<ConnectedBlock> connected_blocks;
    BlockActivationTimings activation_timings;
    uint64_t activation_connected_blocks{0};
    CBlockIndex* block_index{Assert(block_index_store.LookupBlockIndex(block->GetHash()))};
    node::KernelNotifications& notifications{*Assert(m_node.notifications)};
    notifications.m_shutdown_on_fatal_error = false;
    m_node.exit_status.store(EXIT_SUCCESS);

    validation::BlockConnectionCommitPackage commit_package{
        .expected_previous_block = Params().GenesisBlock().GetHash(),
        .commit_context = Consensus::BlockCommitContext{
            .new_best_block = block->GetHash(),
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .effects = Consensus::BlockSpendEffects{},
    };
    CoreBlockConnectionCommitWork commit_work{MakeCoreBlockConnectionCommitWork(
        ChainWorkBlockSnapshot{
            .hash = block->GetHash(),
            .parent_hash = Params().GenesisBlock().GetHash(),
            .height = 1,
            .chain_work = block_index->nChainWork,
        },
        block,
        std::move(commit_package))};

    BlockValidationState commit_state;
    const CoreConnectTipResult committed{CommitCoreBlockConnection(
        {
            .runtime = runtime,
            .undo_writer = block_store,
            .block_index_lookup = block_index_store,
            .block_index_committer = block_index_store,
            .connection_state = connection_state,
            .spend_state_committer = spend_state_committer,
            .connected_blocks = connected_blocks,
            .chain_events = nullptr,
            .current_time = CurrentNodeTime(),
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
        std::move(commit_work), commit_state)};

    BOOST_CHECK(!committed.Succeeded());
    BOOST_CHECK(committed.status == CoreConnectTipStatus::BlockConnectionFailed);
    BOOST_CHECK(commit_state.IsError());
    BOOST_CHECK_EQUAL(commit_state.GetRejectReason(), "forced-tainted-commit");
    BOOST_CHECK_EQUAL(m_node.exit_status.load(), EXIT_FAILURE);
    BOOST_CHECK_EQUAL(chainstate.m_chain.Height(), 0);
    BOOST_CHECK(connected_blocks.empty());
    BOOST_CHECK_EQUAL(activation_connected_blocks, 0U);
}

BOOST_AUTO_TEST_CASE(core_connect_tip_connection_flush_failure_is_fatal_before_tip_reuse)
{
    auto& chainman{*Assert(m_node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{AcceptNewBlockData({
        .chainman = chainman,
        .block = block,
        .state = state,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = CurrentBlockValidationTime(),
    })};
    BOOST_REQUIRE(accepted.ShouldAttemptActivation());

    LOCK(cs_main);
    CoreActivationRuntime runtime{chainman};
    CoreBlockDataStore block_store{chainman.m_blockman};
    CoreBlockIndexStore block_index_store{chainman};
    ThrowingCommitConnectionState connection_state{Params().GenesisBlock().GetHash()};
    TaintedSpendCommitter spend_state_committer;
    std::vector<ConnectedBlock> connected_blocks;
    BlockActivationTimings activation_timings;
    uint64_t activation_connected_blocks{0};
    CBlockIndex* block_index{Assert(block_index_store.LookupBlockIndex(block->GetHash()))};
    node::KernelNotifications& notifications{*Assert(m_node.notifications)};
    notifications.m_shutdown_on_fatal_error = false;
    m_node.exit_status.store(EXIT_SUCCESS);

    validation::BlockConnectionCommitPackage commit_package{
        .expected_previous_block = Params().GenesisBlock().GetHash(),
        .commit_context = Consensus::BlockCommitContext{
            .new_best_block = block->GetHash(),
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .effects = std::nullopt,
    };
    CoreBlockConnectionCommitWork commit_work{MakeCoreBlockConnectionCommitWork(
        ChainWorkBlockSnapshot{
            .hash = block->GetHash(),
            .parent_hash = Params().GenesisBlock().GetHash(),
            .height = 1,
            .chain_work = block_index->nChainWork,
        },
        block,
        std::move(commit_package))};

    BlockValidationState commit_state;
    const CoreConnectTipResult committed{CommitCoreBlockConnection(
        {
            .runtime = runtime,
            .undo_writer = block_store,
            .block_index_lookup = block_index_store,
            .block_index_committer = block_index_store,
            .connection_state = connection_state,
            .spend_state_committer = spend_state_committer,
            .connected_blocks = connected_blocks,
            .chain_events = nullptr,
            .current_time = CurrentNodeTime(),
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
        std::move(commit_work), commit_state)};

    BOOST_CHECK(!committed.Succeeded());
    BOOST_CHECK(committed.status == CoreConnectTipStatus::BlockConnectionFailed);
    BOOST_CHECK(commit_state.IsError());
    BOOST_CHECK_EQUAL(commit_state.GetRejectReason(), "block-connection-flush-failed");
    BOOST_CHECK_EQUAL(m_node.exit_status.load(), EXIT_FAILURE);
    BOOST_CHECK_EQUAL(chainstate.m_chain.Height(), 0);
    BOOST_CHECK(connected_blocks.empty());
    BOOST_CHECK_EQUAL(activation_connected_blocks, 0U);
}

BOOST_AUTO_TEST_CASE(service_commits_prepared_tip_work)
{
    auto& chainman{*Assert(m_node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};

    BlockValidationState state;
    const BlockAcceptanceResult accepted{AcceptNewBlockData({
        .chainman = chainman,
        .block = block,
        .state = state,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = CurrentBlockValidationTime(),
    })};
    BOOST_REQUIRE(accepted.ShouldAttemptActivation());

    const std::optional<NewBlockCandidateContextSnapshot> context{SnapshotAcceptedBlockContext({
        .chainman = chainman,
        .block_hash = block->GetHash(),
    })};
    BOOST_REQUIRE(context);

    BlockValidationState prepare_state;
    std::optional<CoreBlockConnectionCommitWork> commit_work{PrepareAcceptedTipCommitWork({
        .chainman = chainman,
        .context = *context,
        .block = block,
        .state = prepare_state,
    })};
    BOOST_REQUIRE(commit_work);
    BOOST_CHECK(commit_work->block_position.hash == block->GetHash());
    BOOST_CHECK(commit_work->commit_package.expected_previous_block == Params().GenesisBlock().GetHash());

    auto block_checked{std::make_shared<BlockCheckedRecorder>()};
    m_node.validation_signals->RegisterSharedValidationInterface(block_checked);
    BlockValidationState commit_state;
    const BlockActivationResult committed{CommitAcceptedTipCandidate({
        .chainman = chainman,
        .chain_events = nullptr,
        .work = std::move(*commit_work),
        .state = commit_state,
        .time = CurrentBlockValidationTime(),
    })};
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    m_node.validation_signals->UnregisterSharedValidationInterface(block_checked);

    BOOST_REQUIRE(committed.Succeeded());
    BOOST_CHECK_EQUAL(committed.connected_blocks, 1U);
    BOOST_CHECK_EQUAL(WITH_LOCK(chainman.GetMutex(), return chainstate.m_chain.Height()), 1);
    BOOST_CHECK_EQUAL(block_checked->checked_count, 1);
    BOOST_REQUIRE(block_checked->checked_hash);
    BOOST_CHECK(*block_checked->checked_hash == block->GetHash());
    BOOST_CHECK(block_checked->checked_valid);
}

BOOST_AUTO_TEST_CASE(service_test_block_validity_uses_explicit_time)
{
    auto& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};
    const int64_t current_time{static_cast<int64_t>(block->nTime) - MAX_FUTURE_BLOCK_TIME - 1};
    BOOST_REQUIRE_GE(current_time, 0);
    const auto time{BlockValidationTime::FromUnixSeconds(current_time)};
    BOOST_REQUIRE(time);

    const BlockValidationState state{TestActiveBlockValidity({
        .chainman = chainman,
        .block = *block,
        .options = {},
        .time = *time,
    })};

    BOOST_CHECK(!state.IsValid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "time-too-new");
}

BOOST_AUTO_TEST_SUITE_END()
