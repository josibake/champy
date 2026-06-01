// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <arith_uint256.h>
#include <validation/block_data_adapters.h>
#include <validation/block_header_context_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_connection.h>
#include <validation/block_connection_trace.h>
#include <validation/block_script_check_adapters.h>
#include <validation/core_coins_block_connection_state.h>
#include <validation/core_chain_lock.h>
#include <validation/test_block_validity.h>
#include <chain.h>
#include <chainstate.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <consensus/script_checker.h>
#include <consensus/snapshot_spend_state.h>
#include <consensus/block_spend.h>
#include <validation/core_block_commit_adapters.h>
#include <kernel/cs_main.h>
#include <kernel/notifications_interface.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/script_error.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <undo.h>
#include <validation_state.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class FixedSequenceLockTimeView final : public Consensus::SequenceLockTimeView
{
public:
    explicit FixedSequenceLockTimeView(int64_t previous_median_time_past) : m_previous_median_time_past{previous_median_time_past} {}

    [[nodiscard]] int64_t PreviousMedianTimePast(const COutPoint&, int) const override { return m_previous_median_time_past; }

private:
    int64_t m_previous_median_time_past;
};

std::shared_ptr<const Consensus::SequenceLockTimeView> FixedSequenceLockTimes(int64_t previous_median_time_past = 0)
{
    return std::make_shared<FixedSequenceLockTimeView>(previous_median_time_past);
}

class FakeBlockUndoWriter final : public BlockUndoWriter
{
public:
    Consensus::BlockCommitResult<void> WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index) override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        wrote_undo = true;
        undo_index = &index;
        written_undo = blockundo;
        return {};
    }

    bool wrote_undo{false};
    CBlockIndex* undo_index{nullptr};
    CBlockUndo written_undo;
};

class FakeBlockIndexCommitter final : public BlockIndexValidityCommitter
{
public:
    void MarkBlockIndexDirty(CBlockIndex& block_index) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { dirty_index = &block_index; }

    CBlockIndex* dirty_index{nullptr};
};

struct BlockConnectionTraceFixture {
    int64_t blocks_total{0};
    SteadyClock::duration time_check{};
    SteadyClock::duration time_forks{};
    SteadyClock::duration time_connect{};
    SteadyClock::duration time_verify{};
    SteadyClock::duration time_undo{};
    SteadyClock::duration time_index{};
    BlockConnectionTrace trace{
        BlockConnectionTraceCounters{
            .num_blocks_total = blocks_total,
            .time_check = time_check,
            .time_forks = time_forks,
            .time_connect = time_connect,
            .time_verify = time_verify,
            .time_undo = time_undo,
            .time_index = time_index,
        }};
};

class FixedActiveChainView final : public validation::ActiveChainView
{
public:
    explicit FixedActiveChainView(CBlockIndex& tip) : m_tip{tip} {}

    [[nodiscard]] CBlockIndex* Tip() const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return &m_tip; }
    [[nodiscard]] int Height() const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return m_tip.nHeight; }
    [[nodiscard]] CBlockIndex* Next(const CBlockIndex&) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return nullptr; }

private:
    CBlockIndex& m_tip;
};

class FixedBlockHeaderContextProvider final : public BlockHeaderContextProvider
{
public:
    [[nodiscard]] Consensus::BlockHeaderContext BuildContext(const CBlockIndex* previous_index) const override
    {
        if (!previous_index) return {};
        return Consensus::BlockHeaderContext{
            previous_index->nHeight + 1,
            previous_index->GetMedianTimePast(),
            previous_index->GetBlockTime()};
    }
};

class NoopBlockConnectionAttemptGuard final : public validation::BlockConnectionAttemptGuard
{
public:
    void Commit() override {}
};

class SnapshotBlockConnectionSpendState final : public validation::BlockConnectionSpendState
{
public:
    SnapshotBlockConnectionSpendState(
        Consensus::SnapshotSpendState& spend_state,
        std::unique_ptr<Consensus::BlockSpendWorkspace> workspace)
        : m_spend_state{spend_state},
          m_workspace{std::move(workspace)}
    {
    }

    [[nodiscard]] Consensus::BlockSpendWorkspace& Workspace() override { return *m_workspace; }
    [[nodiscard]] Consensus::BlockSpendStateCommitter& Committer() override { return m_spend_state; }

private:
    Consensus::SnapshotSpendState& m_spend_state;
    std::unique_ptr<Consensus::BlockSpendWorkspace> m_workspace;
};

class SnapshotBlockConnectionState final : public validation::BlockConnectionState
{
public:
    [[nodiscard]] uint256 BestBlock() const override { return m_best_block; }
    void SetBestBlock(const uint256& block_hash) override { m_best_block = block_hash; }
    [[nodiscard]] std::unique_ptr<validation::BlockConnectionAttemptGuard> BeginConnectionAttempt() override
    {
        return std::make_unique<NoopBlockConnectionAttemptGuard>();
    }

    [[nodiscard]] Consensus::BlockSpendResult<std::unique_ptr<validation::BlockConnectionSpendState>> BeginBlockSpend(
        const Consensus::BlockSpendContext& context,
        std::shared_ptr<const Consensus::SequenceLockTimeView>) override
    {
        auto workspace{m_spend_state.BeginBlockSpend(context)};
        if (!workspace) return Consensus::Unexpected<Consensus::BlockSpendError>{workspace.error()};

        std::unique_ptr<validation::BlockConnectionSpendState> spend_state{
            std::make_unique<SnapshotBlockConnectionSpendState>(m_spend_state, std::move(*workspace))};
        return std::move(spend_state);
    }

    [[nodiscard]] std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const
    {
        return m_spend_state.GetCoin(outpoint);
    }

    void AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin)
    {
        m_spend_state.AddCoin(outpoint, std::move(coin));
    }

private:
    uint256 m_best_block;
    Consensus::SnapshotSpendState m_spend_state;
};

validation::BlockConnectionCommitRequest MakeBlockConnectionCommitRequest(
    BlockUndoWriter& undo_writer,
    BlockIndexValidityCommitter& block_index_committer,
    BlockConnectionTrace& trace,
    const CBlock& block,
    CBlockIndex& block_index,
    validation::BlockConnectionState& connection_state)
{
    return {
        .runtime = {
            .undo_writer = undo_writer,
            .block_index_committer = block_index_committer,
            .trace = trace,
        },
        .context = {
            .block = block,
            .block_index = block_index,
            .connection_state = connection_state,
        },
    };
}

CTransactionRef MakeCoinbase(CAmount value)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeSpendTx(const COutPoint& prevout, CAmount value)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = prevout;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CBlock MakeBlock(const uint256& previous_block, CAmount coinbase_value, std::vector<CTransactionRef> transactions = {})
{
    CBlock block;
    block.hashPrevBlock = previous_block;
    block.nTime = 1;
    block.vtx.push_back(MakeCoinbase(coinbase_value));
    block.vtx.insert(block.vtx.end(), transactions.begin(), transactions.end());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(block_validation_adapters_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(core_script_validation_cache_is_not_cs_main_locked)
{
    AssertLockNotHeld(::cs_main);

    ValidationCache validation_cache{/*script_execution_cache_bytes=*/1024 * 1024, /*signature_cache_bytes=*/1024 * 1024};
    CoreScriptValidationCache script_cache{validation_cache};
    const CTransactionRef tx{MakeSpendTx(COutPoint{Txid::FromUint256(uint256::ONE), 0}, /*value=*/39)};
    const uint256 entry{script_cache.ExecutionCacheEntry(*tx, script_verify_flags{})};

    BOOST_CHECK(!script_cache.ContainsScriptExecution(entry, /*erase=*/false));
    script_cache.StoreScriptExecution(entry);
    BOOST_CHECK(script_cache.ContainsScriptExecution(entry, /*erase=*/false));
}

BOOST_AUTO_TEST_CASE(core_block_script_checker_runs_from_prepared_outputs_without_cs_main)
{
    AssertLockNotHeld(::cs_main);

    ValidationCache validation_cache{/*script_execution_cache_bytes=*/1024 * 1024, /*signature_cache_bytes=*/1024 * 1024};
    CCheckQueue<CScriptCheck> script_check_queue{/*batch_size=*/128, /*worker_threads_num=*/0};
    validation::CCheckQueueScriptCheckScheduler script_check_scheduler{script_check_queue};
    CoreBlockScriptChecks script_checks{
        script_check_scheduler,
        /*run_checks=*/true,
        /*cache_results=*/true,
        validation_cache};

    const CTransactionRef tx{MakeSpendTx(COutPoint{Txid::FromUint256(uint256::ONE), 0}, /*value=*/39)};
    Consensus::TransactionScriptCheckPlan plan{
        .tx = tx,
        .flags = script_verify_flags{},
        .spent_outputs = {CTxOut{40, CScript{} << OP_TRUE}},
    };

    BOOST_REQUIRE(script_checks.Checker().Check(plan));
    BOOST_REQUIRE(script_checks.Checker().Complete());
}

BOOST_AUTO_TEST_CASE(core_block_script_checker_can_submit_while_cs_main_is_held)
{
    WAIT_LOCK(::cs_main, chain_lock_handle);
    CoreChainLock chain_lock{chain_lock_handle};

    ValidationCache validation_cache{/*script_execution_cache_bytes=*/1024 * 1024, /*signature_cache_bytes=*/1024 * 1024};
    CCheckQueue<CScriptCheck> script_check_queue{/*batch_size=*/128, /*worker_threads_num=*/0};
    validation::CCheckQueueScriptCheckScheduler script_check_scheduler{script_check_queue};
    CoreBlockScriptChecks script_checks{
        script_check_scheduler,
        /*run_checks=*/true,
        /*cache_results=*/true,
        validation_cache,
        &chain_lock};

    const CTransactionRef tx{MakeSpendTx(COutPoint{Txid::FromUint256(uint256::ONE), 0}, /*value=*/39)};
    Consensus::TransactionScriptCheckPlan plan{
        .tx = tx,
        .flags = script_verify_flags{},
        .spent_outputs = {CTxOut{40, CScript{} << OP_TRUE}},
    };

    BOOST_REQUIRE(script_checks.Checker().Check(plan));
    BOOST_REQUIRE(script_checks.Checker().Complete());
    AssertLockHeld(::cs_main);
}

BOOST_AUTO_TEST_CASE(core_block_script_checker_preserves_failure_reason_without_cs_main)
{
    AssertLockNotHeld(::cs_main);

    ValidationCache validation_cache{/*script_execution_cache_bytes=*/1024 * 1024, /*signature_cache_bytes=*/1024 * 1024};
    CCheckQueue<CScriptCheck> script_check_queue{/*batch_size=*/128, /*worker_threads_num=*/0};
    validation::CCheckQueueScriptCheckScheduler script_check_scheduler{script_check_queue};
    CoreBlockScriptChecks script_checks{
        script_check_scheduler,
        /*run_checks=*/true,
        /*cache_results=*/true,
        validation_cache};

    const CTransactionRef tx{MakeSpendTx(COutPoint{Txid::FromUint256(uint256::ONE), 0}, /*value=*/39)};
    Consensus::TransactionScriptCheckPlan plan{
        .tx = tx,
        .flags = script_verify_flags{},
        .spent_outputs = {CTxOut{40, CScript{} << OP_FALSE}},
    };

    const auto result{script_checks.Checker().Check(plan)};
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().issue == Consensus::BlockConsensusIssue::Consensus);
    BOOST_CHECK_EQUAL(
        result.error().reject_reason,
        std::string{"block-script-verify-flag-failed ("} + ScriptErrorString(SCRIPT_ERR_EVAL_FALSE) + ")");
}

BOOST_AUTO_TEST_CASE(core_block_script_checker_reports_queued_failure_without_cs_main)
{
    AssertLockNotHeld(::cs_main);

    ValidationCache validation_cache{/*script_execution_cache_bytes=*/1024 * 1024, /*signature_cache_bytes=*/1024 * 1024};
    CCheckQueue<CScriptCheck> script_check_queue{/*batch_size=*/128, /*worker_threads_num=*/1};
    validation::CCheckQueueScriptCheckScheduler script_check_scheduler{script_check_queue};
    CoreBlockScriptChecks script_checks{
        script_check_scheduler,
        /*run_checks=*/true,
        /*cache_results=*/true,
        validation_cache};

    const CTransactionRef tx{MakeSpendTx(COutPoint{Txid::FromUint256(uint256::ONE), 0}, /*value=*/39)};
    Consensus::TransactionScriptCheckPlan plan{
        .tx = tx,
        .flags = script_verify_flags{},
        .spent_outputs = {CTxOut{40, CScript{} << OP_FALSE}},
    };

    BOOST_REQUIRE(script_checks.Checker().Check(plan));
    const auto result{script_checks.Checker().Complete()};
    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(
        result.error().reject_reason,
        std::string{"block-script-verify-flag-failed ("} + ScriptErrorString(SCRIPT_ERR_EVAL_FALSE) + ")");
}

BOOST_AUTO_TEST_CASE(core_block_effects_writer_uses_storage_adapters)
{
    LOCK(::cs_main);

    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    CBlockIndex block_index;
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    validation::CoreCoinsBlockConnectionState connection_state{coins};
    CoreBlockEffectsWriter writer{undo_writer, block_index_committer, connection_state, block_index};

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.resize(2);
    effects.transaction_effects[1].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = COutPoint{Txid::FromUint256(uint256::ONE), 0},
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{}},
            .height = 7,
            .is_coinbase = true,
        },
    });

    BOOST_REQUIRE(writer.WriteBlockRevertData({}, effects));
    BOOST_CHECK(undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(undo_writer.undo_index, &block_index);
    BOOST_REQUIRE_EQUAL(undo_writer.written_undo.vtxundo.size(), 1U);
    const CTxUndo& transaction_undo{undo_writer.written_undo.vtxundo.front()};
    BOOST_REQUIRE_EQUAL(transaction_undo.vprevout.size(), 1U);
    const Coin& spent_coin{transaction_undo.vprevout.front()};
    BOOST_CHECK(spent_coin.IsCoinBase());
    BOOST_CHECK_EQUAL(spent_coin.nHeight, 7);

    const Consensus::BlockCommitContext context{.new_best_block = uint256::ONE};
    BOOST_REQUIRE(writer.CommitBlockMetadata(context, effects));
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, &block_index);
    BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK(coins.GetBestBlock() == uint256::ONE);
}

BOOST_AUTO_TEST_CASE(block_connection_engine_accepts_snapshot_state_backend)
{
    LOCK(::cs_main);

    const uint256 previous_hash{uint256::ONE};
    CBlock block{MakeBlock(previous_hash, /*coinbase_value=*/50)};
    const uint256 block_hash{block.GetHash()};
    CBlockIndex previous_index;
    previous_index.phashBlock = &previous_hash;
    previous_index.nHeight = 0;

    CBlockIndex block_index{block};
    block_index.pprev = &previous_index;
    block_index.nHeight = 1;
    block_index.phashBlock = &block_hash;

    SnapshotBlockConnectionState connection_state;
    connection_state.SetBestBlock(previous_hash);

    kernel::Notifications notifications;
    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    Consensus::DirectBlockScriptChecker script_checker;
    int64_t blocks_total{0};
    SteadyClock::duration time_check{};
    SteadyClock::duration time_forks{};
    SteadyClock::duration time_connect{};
    SteadyClock::duration time_verify{};
    SteadyClock::duration time_undo{};
    SteadyClock::duration time_index{};
    BlockConnectionTrace trace{
        BlockConnectionTraceCounters{
            .num_blocks_total = blocks_total,
            .time_check = time_check,
            .time_forks = time_forks,
            .time_connect = time_connect,
            .time_verify = time_verify,
            .time_undo = time_undo,
            .time_index = time_index,
        }};

    const Consensus::BlockConsensusContext consensus_context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{
            .new_best_block = block_hash,
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .block_subsidy = 50,
    };
    validation::BlockConnectionRequest request{
        .runtime = {
            .notifications = notifications,
            .undo_writer = undo_writer,
            .block_index_committer = block_index_committer,
            .script_checker = script_checker,
            .trace = trace,
        },
        .context = {
            .consensus_params = Params().GetConsensus(),
            .consensus_context = consensus_context,
            .sequence_lock_times = FixedSequenceLockTimes(),
            .spend_options = Consensus::BlockSpendConsensusOptions{
                .check_no_unspent_output_overwrite = true,
            },
        },
        .block = block,
        .block_index = block_index,
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.Connect(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);
    BOOST_REQUIRE(engine.Commit(MakeBlockConnectionCommitRequest(undo_writer, block_index_committer, trace, block, block_index, connection_state), std::move(*validated.commit_package), state).Succeeded());

    BOOST_CHECK(connection_state.BestBlock() == block_hash);
    BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, &block_index);
    BOOST_CHECK(undo_writer.wrote_undo);

    const COutPoint coinbase_outpoint{block.vtx[0]->GetHash(), 0};
    const auto coinbase_coin{connection_state.GetCoin(coinbase_outpoint)};
    BOOST_REQUIRE(coinbase_coin);
    BOOST_CHECK_EQUAL(coinbase_coin->height, 1);
    BOOST_CHECK(coinbase_coin->is_coinbase);
    BOOST_CHECK_EQUAL(coinbase_coin->output.nValue, 50);
}

BOOST_AUTO_TEST_CASE(block_connection_engine_returns_commit_package_without_mutation)
{
    LOCK(::cs_main);

    const uint256 previous_hash{uint256::ONE};
    CBlock block{MakeBlock(previous_hash, /*coinbase_value=*/50)};
    const uint256 block_hash{block.GetHash()};
    CBlockIndex previous_index;
    previous_index.phashBlock = &previous_hash;
    previous_index.nHeight = 0;

    CBlockIndex block_index{block};
    block_index.pprev = &previous_index;
    block_index.nHeight = 1;
    block_index.phashBlock = &block_hash;

    SnapshotBlockConnectionState connection_state;
    connection_state.SetBestBlock(previous_hash);

    kernel::Notifications notifications;
    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    Consensus::DirectBlockScriptChecker script_checker;
    BlockConnectionTraceFixture trace;

    const Consensus::BlockConsensusContext consensus_context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{
            .new_best_block = block_hash,
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .block_subsidy = 50,
    };
    validation::BlockConnectionRequest request{
        .runtime = {
            .notifications = notifications,
            .undo_writer = undo_writer,
            .block_index_committer = block_index_committer,
            .script_checker = script_checker,
            .trace = trace.trace,
        },
        .context = {
            .consensus_params = Params().GetConsensus(),
            .consensus_context = consensus_context,
            .sequence_lock_times = FixedSequenceLockTimes(),
            .spend_options = Consensus::BlockSpendConsensusOptions{
                .check_no_unspent_output_overwrite = true,
            },
        },
        .block = block,
        .block_index = block_index,
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.Connect(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);

    const COutPoint coinbase_outpoint{block.vtx[0]->GetHash(), 0};
    BOOST_CHECK(connection_state.BestBlock() == previous_hash);
    BOOST_CHECK(!connection_state.GetCoin(coinbase_outpoint));
    BOOST_CHECK(!block_index.IsValid(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK(!undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, nullptr);

    const auto commit_request{
        MakeBlockConnectionCommitRequest(undo_writer, block_index_committer, trace.trace, block, block_index, connection_state)};
    BOOST_REQUIRE(engine.Commit(commit_request, std::move(*validated.commit_package), state).Succeeded());

    BOOST_CHECK(connection_state.BestBlock() == block_hash);
    const auto coinbase_coin{connection_state.GetCoin(coinbase_outpoint)};
    BOOST_REQUIRE(coinbase_coin);
    BOOST_CHECK_EQUAL(coinbase_coin->height, 1);
    BOOST_CHECK(coinbase_coin->is_coinbase);
    BOOST_CHECK_EQUAL(coinbase_coin->output.nValue, 50);
    BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK(undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, &block_index);
}

BOOST_AUTO_TEST_CASE(block_connection_commit_rejects_stale_parent)
{
    LOCK(::cs_main);

    const uint256 previous_hash{uint256::ONE};
    CBlock block{MakeBlock(previous_hash, /*coinbase_value=*/50)};
    const uint256 block_hash{block.GetHash()};
    CBlockIndex previous_index;
    previous_index.phashBlock = &previous_hash;
    previous_index.nHeight = 0;

    CBlockIndex block_index{block};
    block_index.pprev = &previous_index;
    block_index.nHeight = 1;
    block_index.phashBlock = &block_hash;

    SnapshotBlockConnectionState connection_state;
    connection_state.SetBestBlock(previous_hash);

    kernel::Notifications notifications;
    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    Consensus::DirectBlockScriptChecker script_checker;
    BlockConnectionTraceFixture trace;

    const Consensus::BlockConsensusContext consensus_context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{
            .new_best_block = block_hash,
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .block_subsidy = 50,
    };
    validation::BlockConnectionRequest request{
        .runtime = {
            .notifications = notifications,
            .undo_writer = undo_writer,
            .block_index_committer = block_index_committer,
            .script_checker = script_checker,
            .trace = trace.trace,
        },
        .context = {
            .consensus_params = Params().GetConsensus(),
            .consensus_context = consensus_context,
            .sequence_lock_times = FixedSequenceLockTimes(),
            .spend_options = Consensus::BlockSpendConsensusOptions{
                .check_no_unspent_output_overwrite = true,
            },
        },
        .block = block,
        .block_index = block_index,
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.Connect(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);

    connection_state.SetBestBlock(uint256{2});
    BOOST_CHECK(!engine.Commit(
        MakeBlockConnectionCommitRequest(undo_writer, block_index_committer, trace.trace, block, block_index, connection_state),
        std::move(*validated.commit_package),
        state).Succeeded());

    BOOST_CHECK(state.IsError());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "stale block connection");
    BOOST_CHECK(!undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, nullptr);
}

BOOST_AUTO_TEST_CASE(block_connection_engine_validates_spends_with_snapshot_state_backend)
{
    LOCK(::cs_main);

    const uint256 previous_hash{uint256::ONE};
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef spend_tx{MakeSpendTx(prevout, /*value=*/39)};
    CBlock block{MakeBlock(previous_hash, /*coinbase_value=*/50, {spend_tx})};
    const uint256 block_hash{block.GetHash()};
    CBlockIndex previous_index;
    previous_index.phashBlock = &previous_hash;
    previous_index.nHeight = 1;

    CBlockIndex block_index{block};
    block_index.pprev = &previous_index;
    block_index.nHeight = 2;
    block_index.phashBlock = &block_hash;

    SnapshotBlockConnectionState connection_state;
    connection_state.SetBestBlock(previous_hash);
    connection_state.AddCoin(
        prevout,
        Consensus::CoinSnapshot{
            .output = CTxOut{40, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = false,
        });

    kernel::Notifications notifications;
    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    Consensus::DirectBlockScriptChecker script_checker;
    int64_t blocks_total{0};
    SteadyClock::duration time_check{};
    SteadyClock::duration time_forks{};
    SteadyClock::duration time_connect{};
    SteadyClock::duration time_verify{};
    SteadyClock::duration time_undo{};
    SteadyClock::duration time_index{};
    BlockConnectionTrace trace{
        BlockConnectionTraceCounters{
            .num_blocks_total = blocks_total,
            .time_check = time_check,
            .time_forks = time_forks,
            .time_connect = time_connect,
            .time_verify = time_verify,
            .time_undo = time_undo,
            .time_index = time_index,
        }};

    const Consensus::BlockConsensusContext consensus_context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{
            .new_best_block = block_hash,
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .block_subsidy = 50,
    };
    validation::BlockConnectionRequest request{
        .runtime = {
            .notifications = notifications,
            .undo_writer = undo_writer,
            .block_index_committer = block_index_committer,
            .script_checker = script_checker,
            .trace = trace,
        },
        .context = {
            .consensus_params = Params().GetConsensus(),
            .consensus_context = consensus_context,
            .sequence_lock_times = FixedSequenceLockTimes(),
            .spend_options = Consensus::BlockSpendConsensusOptions{
                .check_no_unspent_output_overwrite = true,
            },
        },
        .block = block,
        .block_index = block_index,
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.Connect(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);
    BOOST_REQUIRE(engine.Commit(MakeBlockConnectionCommitRequest(undo_writer, block_index_committer, trace, block, block_index, connection_state), std::move(*validated.commit_package), state).Succeeded());

    BOOST_CHECK(!connection_state.GetCoin(prevout));
    const COutPoint spend_outpoint{spend_tx->GetHash(), 0};
    const auto spend_coin{connection_state.GetCoin(spend_outpoint)};
    BOOST_REQUIRE(spend_coin);
    BOOST_CHECK_EQUAL(spend_coin->height, 2);
    BOOST_CHECK(!spend_coin->is_coinbase);
    BOOST_CHECK_EQUAL(spend_coin->output.nValue, 39);
    BOOST_CHECK(undo_writer.wrote_undo);
}

BOOST_AUTO_TEST_CASE(test_block_validity_accepts_snapshot_state_backend)
{
    LOCK(::cs_main);

    const Consensus::Params& consensus_params{Params().GetConsensus()};
    const uint256 previous_hash{uint256::ONE};
    CBlockIndex tip;
    tip.phashBlock = &previous_hash;
    tip.nHeight = 0;
    tip.nTime = 1;
    tip.nBits = UintToArith256(consensus_params.powLimit).GetCompact();

    CBlock block{MakeBlock(previous_hash, /*coinbase_value=*/50)};
    block.nVersion = 1;
    block.nTime = tip.nTime + 1;
    block.nBits = GetNextWorkRequired(&tip, &block, consensus_params);
    block.hashMerkleRoot = BlockMerkleRoot(block);

    FixedActiveChainView active_chain{tip};
    FixedBlockHeaderContextProvider header_context;
    SnapshotBlockConnectionState connection_state;
    connection_state.SetBestBlock(previous_hash);
    kernel::Notifications notifications;
    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    Consensus::DirectBlockScriptChecker script_checker;
    int64_t blocks_total{0};
    SteadyClock::duration time_check{};
    SteadyClock::duration time_forks{};
    SteadyClock::duration time_connect{};
    SteadyClock::duration time_verify{};
    SteadyClock::duration time_undo{};
    SteadyClock::duration time_index{};
    BlockConnectionTrace trace{
        BlockConnectionTraceCounters{
            .num_blocks_total = blocks_total,
            .time_check = time_check,
            .time_forks = time_forks,
            .time_connect = time_connect,
            .time_verify = time_verify,
            .time_undo = time_undo,
            .time_index = time_index,
        }};

    TestBlockValidityRequest request{
        .active_chain = active_chain,
        .consensus_params = consensus_params,
        .header_context = header_context,
        .connection_state = connection_state,
        .undo_writer = undo_writer,
        .block_index_committer = block_index_committer,
        .notifications = notifications,
        .script_checker = script_checker,
        .trace = trace,
        .sequence_lock_times = FixedSequenceLockTimes(),
    };

    const BlockValidationState state{TestBlockValidity(
        request,
        block,
        Consensus::BlockCheckOptions{.check_pow = false},
        BlockValidationTime{
            .current_time_seconds = block.nTime,
            .max_future_block_time = block.nTime + 1,
        })};
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(connection_state.BestBlock() == previous_hash);
    BOOST_CHECK(!undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
