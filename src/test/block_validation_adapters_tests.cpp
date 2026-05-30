// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_connection.h>
#include <validation/block_connection_trace.h>
#include <validation/core_coins_block_connection_state.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <consensus/script_checker.h>
#include <consensus/snapshot_spend_state.h>
#include <consensus/block_spend.h>
#include <validation/core_block_commit_adapters.h>
#include <kernel/cs_main.h>
#include <kernel/notifications_interface.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <undo.h>
#include <validation_state.h>

#include <memory>
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
    BOOST_REQUIRE(validation::BlockConnectionEngine{}.Connect(request, state).Succeeded());

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
    BOOST_REQUIRE(validation::BlockConnectionEngine{}.Connect(request, state).Succeeded());

    BOOST_CHECK(!connection_state.GetCoin(prevout));
    const COutPoint spend_outpoint{spend_tx->GetHash(), 0};
    const auto spend_coin{connection_state.GetCoin(spend_outpoint)};
    BOOST_REQUIRE(spend_coin);
    BOOST_CHECK_EQUAL(spend_coin->height, 2);
    BOOST_CHECK(!spend_coin->is_coinbase);
    BOOST_CHECK_EQUAL(spend_coin->output.nValue, 39);
    BOOST_CHECK(undo_writer.wrote_undo);
}

BOOST_AUTO_TEST_SUITE_END()
