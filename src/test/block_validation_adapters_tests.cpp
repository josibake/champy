// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <chainstate.h>
#include <coins.h>
#include <consensus/block_commit.h>
#include <consensus/expected.h>
#include <consensus/block_spend.h>
#include <consensus/merkle.h>
#include <consensus/script_checker.h>
#include <consensus/spend_state_batch.h>
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
#include <validation/block_connection.h>
#include <validation/block_connection_trace.h>
#include <validation/block_data_adapters.h>
#include <validation/block_validation.h>
#include <validation/block_validation_error.h>
#include <validation/block_header_context_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_replay.h>
#include <validation/block_script_check_adapters.h>
#include <validation/core_block_commit_adapters.h>
#include <validation/core_block_connection_snapshot.h>
#include <validation/core_chain_lock.h>
#include <validation/core_coins_block_connection_state.h>
#include <validation/core_check_queue_script_task_executor.h>
#include <validation/snapshot_block_connection_state.h>
#include <validation/test_block_validity.h>
#include <validation/verify_db.h>
#include <validation_state.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using validation::SnapshotBlockConnectionState;

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

class FailingSpendCommitter final : public Consensus::SpendCommitter
{
public:
    explicit FailingSpendCommitter(Consensus::BlockCommitError error) : m_error{std::move(error)} {}

    Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Consensus::Unexpected<Consensus::BlockCommitError>{m_error};
    }

private:
    Consensus::BlockCommitError m_error;
};

class FakeReplayBlockReader final : public BlockDataReader
{
public:
    BlockDataReadResult ReadBlock(const BlockDataReadRequest& request) override
    {
        requests.push_back(request);
        return CBlock{};
    }

    BlockDataReadResult ReadBlockFromPosition(const FlatFilePos&, const std::optional<uint256>&) override
    {
        return util::Unexpected{BlockDataReadError::IoError};
    }

    std::vector<BlockDataReadRequest> requests;
};

class FakeReplayUndoReader final : public BlockUndoReader
{
public:
    BlockUndoReadResult ReadBlockUndo(const BlockUndoReadRequest&) override { return util::Unexpected{BlockUndoReadError::IoError}; }
};

class ThrowingScriptTaskExecutor final : public validation::ScriptTaskExecutor
{
public:
    [[nodiscard]] bool ExecutesInline() const noexcept override { return false; }

    [[nodiscard]] validation::ScriptExecutionResult Execute(std::vector<validation::ScriptTask>&&) override
    {
        throw std::runtime_error{"script executor failed"};
    }
};

class FakeReplayIndex final : public BlockReplayIndex
{
public:
    explicit FakeReplayIndex(BlockReplayBlock block) : m_block{std::move(block)} {}

    [[nodiscard]] std::optional<BlockReplayBlock> LookupBlock(const uint256& block_hash) const override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        if (block_hash == m_block.hash) return m_block;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<BlockReplayBlock> Previous(const BlockReplayBlock&) const override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        return std::nullopt;
    }

    [[nodiscard]] std::optional<BlockReplayBlock> AncestorAtHeight(const BlockReplayBlock& block, int height) const override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        if (block.hash == m_block.hash && height == m_block.height) return m_block;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<BlockReplayBlock> LastCommonAncestor(const BlockReplayBlock&, const BlockReplayBlock&) const override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        return std::nullopt;
    }

private:
    BlockReplayBlock m_block;
};

class FakeReplayCoins final : public BlockReplayCoins
{
public:
    explicit FakeReplayCoins(std::vector<uint256> heads) : m_heads{std::move(heads)} {}

    [[nodiscard]] std::vector<uint256> GetHeadBlocks() const override { return m_heads; }

    [[nodiscard]] DisconnectResult DisconnectBlock(BlockUndoReader&, const CBlock&, const BlockReplayBlock&) override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        return DISCONNECT_FAILED;
    }

    [[nodiscard]] bool RollforwardBlock(const CBlock&, const BlockReplayBlock& block) override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        rolled_forward.push_back(block.hash);
        return true;
    }

    void SetBestBlock(const uint256& block_hash) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        best_block = block_hash;
    }

    void Flush() override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { flushed = true; }

    std::vector<uint256> rolled_forward;
    uint256 best_block;
    bool flushed{false};

private:
    std::vector<uint256> m_heads;
};

struct BlockConnectionTraceFixture {
    BlockConnectionTrace trace;
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

class FakeBlockSpendJoiner final : public Consensus::BlockSpendJoiner
{
public:
    Consensus::BlockSpentOutputJoin joined_inputs;

    Consensus::BlockSpentOutputJoin Join(std::span<const CTransactionRef>, int) const override
    {
        return joined_inputs;
    }
};

validation::BlockConnectionBlockPosition BlockPositionFor(const CBlockIndex& block_index)
{
    return {
        .hash = block_index.GetBlockHash(),
        .parent_hash = block_index.pprev == nullptr ? uint256{} : block_index.pprev->GetBlockHash(),
        .height = block_index.nHeight,
    };
}

validation::BlockConnectionCommitRequest MakeBlockConnectionCommitRequest(
    CoreBlockConnectionCommitTarget& commit_target,
    Consensus::SpendCommitter& spend_state_committer,
    BlockConnectionTrace& trace,
    const CBlock& block,
    validation::BlockConnectionState& connection_state) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return {
        .runtime = {
            .revert_data_writer = commit_target.RevertDataWriter(),
            .spend_state_committer = spend_state_committer,
            .metadata_committer = commit_target.MetadataCommitter(),
            .trace = trace,
        },
        .context = {
            .block = block,
            .block_position = commit_target.BlockPosition(),
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

void CheckCommitFailure(
    const Consensus::BlockCommitResult<void>& commit,
    const std::string& reason,
    Consensus::BlockCommitFailureState failure_state)
{
    BOOST_REQUIRE(!commit);
    BOOST_CHECK_EQUAL(commit.error().reject_reason, reason);
    BOOST_CHECK(commit.error().failure_state == failure_state);
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(block_validation_adapters_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(block_commit_error_application_accepts_typed_failure_states)
{
    BlockValidationState unchanged_state;
    BOOST_CHECK(!ApplyBlockCommitError(
        unchanged_state,
        Consensus::BlockCommitError{
            .failure_state = Consensus::BlockCommitFailureState::Unchanged,
            .reject_reason = "commit-unchanged",
        }));
    BOOST_CHECK(unchanged_state.IsError());
    BOOST_CHECK_EQUAL(unchanged_state.GetRejectReason(), "commit-unchanged");

    BlockValidationState tainted_state;
    BOOST_CHECK(!ApplyBlockCommitError(
        tainted_state,
        Consensus::BlockCommitError{
            .failure_state = Consensus::BlockCommitFailureState::Tainted,
            .reject_reason = "commit-tainted",
        }));
    BOOST_CHECK(tainted_state.IsError());
    BOOST_CHECK_EQUAL(tainted_state.GetRejectReason(), "commit-tainted");
}

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
    validation::DirectScriptTaskExecutor script_task_executor;
    CoreBlockScriptChecks script_checks{
        script_task_executor,
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
    validation::ThreadPoolScriptTaskExecutor script_task_executor{/*worker_threads=*/1};
    CoreBlockScriptChecks script_checks{
        script_task_executor,
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
    validation::DirectScriptTaskExecutor script_task_executor;
    CoreBlockScriptChecks script_checks{
        script_task_executor,
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
    validation::ThreadPoolScriptTaskExecutor script_task_executor{/*worker_threads=*/1};
    CoreBlockScriptChecks script_checks{
        script_task_executor,
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

BOOST_AUTO_TEST_CASE(core_block_script_checker_translates_executor_exceptions_to_runtime_errors)
{
    AssertLockNotHeld(::cs_main);

    ValidationCache validation_cache{/*script_execution_cache_bytes=*/1024 * 1024, /*signature_cache_bytes=*/1024 * 1024};
    ThrowingScriptTaskExecutor script_task_executor;
    CoreBlockScriptChecks script_checks{
        script_task_executor,
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
    const auto result{script_checks.Checker().Complete()};
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().issue == Consensus::BlockConsensusIssue::ValidationRuntime);
    BOOST_REQUIRE(result.error().runtime_issue);
    BOOST_CHECK(*result.error().runtime_issue == Consensus::ValidationRuntimeIssue::SystemError);
    BOOST_CHECK_EQUAL(result.error().reject_reason, "script-task-execution-failed");
    BOOST_CHECK_EQUAL(result.error().debug_message, "script executor failed");
}

BOOST_AUTO_TEST_CASE(core_block_script_checker_keeps_legacy_checkqueue_adapter_explicit)
{
    AssertLockNotHeld(::cs_main);

    ValidationCache validation_cache{/*script_execution_cache_bytes=*/1024 * 1024, /*signature_cache_bytes=*/1024 * 1024};
    CCheckQueue<validation::ScriptTask> script_check_queue{/*batch_size=*/128, /*worker_threads_num=*/1};
    validation::CoreCheckQueueScriptTaskExecutor script_task_executor{script_check_queue};
    CoreBlockScriptChecks script_checks{
        script_task_executor,
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

BOOST_AUTO_TEST_CASE(core_block_commit_adapters_split_revert_data_and_metadata)
{
    LOCK(::cs_main);

    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    CBlockIndex block_index;
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    validation::CoreCoinsBlockConnectionState connection_state{coins};
    CoreBlockRevertDataWriter revert_data_writer{undo_writer, block_index};
    CoreBlockMetadataCommitter metadata_committer{block_index_committer, connection_state, block_index};

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

    BOOST_REQUIRE(revert_data_writer.WriteBlockRevertData({}, effects));
    BOOST_CHECK(undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(undo_writer.undo_index, &block_index);
    BOOST_REQUIRE_EQUAL(undo_writer.written_undo.vtxundo.size(), 1U);
    const CTxUndo& transaction_undo{undo_writer.written_undo.vtxundo.front()};
    BOOST_REQUIRE_EQUAL(transaction_undo.vprevout.size(), 1U);
    const Coin& spent_coin{transaction_undo.vprevout.front()};
    BOOST_CHECK(spent_coin.IsCoinBase());
    BOOST_CHECK_EQUAL(spent_coin.nHeight, 7);

    const Consensus::BlockCommitContext context{.new_best_block = uint256::ONE};
    BOOST_REQUIRE(metadata_committer.CommitBlockMetadata(context, effects));
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, &block_index);
    BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK(coins.GetBestBlock() == uint256::ONE);
}

BOOST_AUTO_TEST_CASE(core_block_spend_effects_committer_applies_value_effects)
{
    LOCK(::cs_main);

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreBlockSpendEffectsCommitter committer{coins};

    const COutPoint spent_outpoint{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(spent_outpoint, Coin{CTxOut{40, CScript{} << OP_TRUE}, 7, false}, /*possible_overwrite=*/false);

    const COutPoint created_outpoint{Txid::FromUint256(uint256{2}), 0};
    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.resize(1);
    effects.transaction_effects[0].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = spent_outpoint,
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{40, CScript{} << OP_TRUE},
            .height = 7,
            .is_coinbase = false,
        },
    });
    effects.transaction_effects[0].creates.push_back(Consensus::CreatedCoinEffect{
        .outpoint = created_outpoint,
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{39, CScript{} << OP_TRUE},
            .height = 8,
            .is_coinbase = false,
        },
    });

    const Consensus::BlockCommitContext context{
        .new_best_block = uint256{3},
        .block_height = 8,
    };
    BOOST_REQUIRE(committer.CommitSpendState(context, effects));
    BOOST_CHECK(!coins.HaveCoin(spent_outpoint));
    const auto created{coins.GetCoin(created_outpoint)};
    BOOST_REQUIRE(created);
    BOOST_CHECK_EQUAL(created->out.nValue, 39);
    BOOST_CHECK_EQUAL(created->nHeight, 8);
}

BOOST_AUTO_TEST_CASE(core_block_spend_effects_committer_rejects_stale_effects)
{
    LOCK(::cs_main);

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreBlockSpendEffectsCommitter committer{coins};

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.resize(1);
    effects.transaction_effects[0].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = COutPoint{Txid::FromUint256(uint256::ONE), 0},
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{40, CScript{} << OP_TRUE},
            .height = 7,
            .is_coinbase = false,
        },
    });

    const auto commit{committer.CommitSpendState(Consensus::BlockCommitContext{}, effects)};
    CheckCommitFailure(commit, "stale block spend state", Consensus::BlockCommitFailureState::Unchanged);
}

BOOST_AUTO_TEST_CASE(core_block_spend_effects_committer_failure_leaves_parent_cache_unchanged)
{
    LOCK(::cs_main);

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreBlockSpendEffectsCommitter committer{coins};

    const COutPoint first_spend{Txid::FromUint256(uint256{10}), 0};
    const COutPoint missing_second_spend{Txid::FromUint256(uint256{11}), 0};
    coins.AddCoin(first_spend, Coin{CTxOut{40, CScript{} << OP_TRUE}, 7, false}, /*possible_overwrite=*/false);

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.resize(1);
    effects.transaction_effects[0].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = first_spend,
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{40, CScript{} << OP_TRUE},
            .height = 7,
            .is_coinbase = false,
        },
    });
    effects.transaction_effects[0].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = missing_second_spend,
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{1, CScript{} << OP_TRUE},
            .height = 7,
            .is_coinbase = false,
        },
    });

    const auto commit{committer.CommitSpendState(Consensus::BlockCommitContext{}, effects)};

    CheckCommitFailure(commit, "stale block spend state", Consensus::BlockCommitFailureState::Unchanged);
    const auto first_after_failure{coins.GetCoin(first_spend)};
    BOOST_REQUIRE(first_after_failure);
    BOOST_CHECK_EQUAL(first_after_failure->out.nValue, 40);
}

BOOST_AUTO_TEST_CASE(core_block_spend_effects_committer_rejects_mismatched_spent_coin_snapshot)
{
    LOCK(::cs_main);

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreBlockSpendEffectsCommitter committer{coins};

    const COutPoint spent_outpoint{Txid::FromUint256(uint256{12}), 0};
    coins.AddCoin(spent_outpoint, Coin{CTxOut{41, CScript{} << OP_TRUE}, 7, false}, /*possible_overwrite=*/false);

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.resize(1);
    effects.transaction_effects[0].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = spent_outpoint,
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{40, CScript{} << OP_TRUE},
            .height = 7,
            .is_coinbase = false,
        },
    });

    const auto commit{committer.CommitSpendState(Consensus::BlockCommitContext{}, effects)};

    CheckCommitFailure(commit, "stale block spend state", Consensus::BlockCommitFailureState::Unchanged);
    const auto coin_after_failure{coins.GetCoin(spent_outpoint)};
    BOOST_REQUIRE(coin_after_failure);
    BOOST_CHECK_EQUAL(coin_after_failure->out.nValue, 41);
}

BOOST_AUTO_TEST_CASE(core_block_spend_effects_committer_flush_conflict_reports_tainted_state)
{
    LOCK(::cs_main);

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreBlockSpendEffectsCommitter committer{coins};

    const COutPoint spent_outpoint{Txid::FromUint256(uint256{13}), 0};
    const COutPoint created_outpoint{Txid::FromUint256(uint256{14}), 0};
    coins.AddCoin(spent_outpoint, Coin{CTxOut{40, CScript{} << OP_TRUE}, 7, false}, /*possible_overwrite=*/false);
    coins.AddCoin(created_outpoint, Coin{CTxOut{1, CScript{} << OP_TRUE}, 7, false}, /*possible_overwrite=*/false);

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.resize(1);
    effects.transaction_effects[0].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = spent_outpoint,
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{40, CScript{} << OP_TRUE},
            .height = 7,
            .is_coinbase = false,
        },
    });
    effects.transaction_effects[0].creates.push_back(Consensus::CreatedCoinEffect{
        .outpoint = created_outpoint,
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{39, CScript{} << OP_TRUE},
            .height = 8,
            .is_coinbase = false,
        },
    });

    const auto commit{committer.CommitSpendState(Consensus::BlockCommitContext{
        .new_best_block = uint256{},
        .block_height = 8,
        .previous_median_time_past = 0,
    }, effects)};

    CheckCommitFailure(commit, "stale block flush state", Consensus::BlockCommitFailureState::Tainted);
}

BOOST_AUTO_TEST_CASE(block_connection_commit_preserves_tainted_commit_failure_state)
{
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

    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    FailingSpendCommitter spend_state_committer{Consensus::BlockCommitError{
        .failure_state = Consensus::BlockCommitFailureState::Tainted,
        .reject_reason = "forced-tainted-commit",
    }};
    BlockConnectionTrace trace;
    CoreBlockConnectionCommitTarget commit_target{
        undo_writer,
        block_index_committer,
        connection_state,
        block_index};
    validation::BlockConnectionCommitPackage package{
        .expected_previous_block = previous_hash,
        .commit_context = Consensus::BlockCommitContext{
            .new_best_block = block_hash,
            .block_height = 1,
            .previous_median_time_past = 0,
        },
        .effects = Consensus::BlockSpendEffects{},
    };

    BlockValidationState state;
    LOCK(::cs_main);
    const validation::BlockConnectionResult result{validation::BlockConnectionEngine{}.Commit(
        MakeBlockConnectionCommitRequest(commit_target, spend_state_committer, trace, block, connection_state),
        std::move(package),
        state)};

    BOOST_CHECK(!result.Succeeded());
    BOOST_REQUIRE(result.commit_failure_state);
    BOOST_CHECK(*result.commit_failure_state == Consensus::BlockCommitFailureState::Tainted);
    BOOST_CHECK(result.HasTaintedCommitFailure());
    BOOST_CHECK(state.IsError());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "forced-tainted-commit");
}

BOOST_AUTO_TEST_CASE(core_block_connection_snapshot_materializes_required_coins)
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

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    coins.SetBestBlock(previous_hash);
    coins.AddCoin(prevout, Coin{CTxOut{40, CScript{} << OP_TRUE}, 1, false}, /*possible_overwrite=*/false);
    const COutPoint created_collision{spend_tx->GetHash(), 0};
    coins.AddCoin(created_collision, Coin{CTxOut{1, CScript{} << OP_TRUE}, 1, false}, /*possible_overwrite=*/false);

    const SnapshotBlockConnectionState snapshot{validation::SnapshotCoreBlockConnectionState(block, block_index, coins)};

    BOOST_CHECK(snapshot.BestBlock() == previous_hash);
    BOOST_CHECK(snapshot.GetCoin(prevout).has_value());
    BOOST_CHECK(snapshot.GetCoin(created_collision).has_value());
    BOOST_CHECK(!snapshot.GetCoin(COutPoint{Txid::FromUint256(uint256{9}), 0}).has_value());
}

BOOST_AUTO_TEST_CASE(block_connection_engine_validates_snapshot_and_commits_to_core_state)
{
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

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    {
        LOCK(::cs_main);
        coins.SetBestBlock(previous_hash);
        coins.AddCoin(prevout, Coin{CTxOut{40, CScript{} << OP_TRUE}, 1, false}, /*possible_overwrite=*/false);
    }

    SnapshotBlockConnectionState snapshot_state;
    {
        LOCK(::cs_main);
        snapshot_state = validation::SnapshotCoreBlockConnectionState(block, block_index, coins);
    }

    kernel::Notifications notifications;
    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    Consensus::DirectBlockScriptChecker script_checker;
    BlockConnectionTrace trace;

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
        .block_position = BlockPositionFor(block_index),
        .connection_state = snapshot_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    AssertLockNotHeld(::cs_main);
    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.ConnectPrepared(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);

    validation::CoreCoinsBlockConnectionState core_connection_state{coins};
    CoreBlockSpendEffectsCommitter spend_state_committer{coins};
    CoreBlockConnectionCommitTarget commit_target{
        undo_writer,
        block_index_committer,
        core_connection_state,
        block_index};
    {
        LOCK(::cs_main);
        BOOST_REQUIRE(engine.Commit(
                                MakeBlockConnectionCommitRequest(commit_target, spend_state_committer, trace, block, core_connection_state),
                                std::move(*validated.commit_package),
                                state)
                          .Succeeded());

        BOOST_CHECK(coins.GetBestBlock() == block_hash);
        BOOST_CHECK(!coins.HaveCoin(prevout));
        const COutPoint spend_outpoint{spend_tx->GetHash(), 0};
        const auto spend_coin{coins.GetCoin(spend_outpoint)};
        BOOST_REQUIRE(spend_coin);
        BOOST_CHECK_EQUAL(spend_coin->out.nValue, 39);
        BOOST_CHECK_EQUAL(spend_coin->nHeight, 2);
        BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    }
}

BOOST_AUTO_TEST_CASE(block_connection_engine_accepts_snapshot_state_backend)
{
    AssertLockNotHeld(::cs_main);

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
    BlockConnectionTrace trace;

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
        .block_position = BlockPositionFor(block_index),
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.ConnectPrepared(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);
    CoreBlockConnectionCommitTarget commit_target{
        undo_writer,
        block_index_committer,
        connection_state,
        block_index};
    {
        LOCK(::cs_main);
        BOOST_REQUIRE(engine.Commit(MakeBlockConnectionCommitRequest(commit_target, connection_state.Committer(), trace, block, connection_state), std::move(*validated.commit_package), state).Succeeded());
    }

    BOOST_CHECK(connection_state.BestBlock() == block_hash);
    {
        LOCK(::cs_main);
        BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    }
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
    AssertLockNotHeld(::cs_main);

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
        .block_position = BlockPositionFor(block_index),
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.ConnectPrepared(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);

    const COutPoint coinbase_outpoint{block.vtx[0]->GetHash(), 0};
    BOOST_CHECK(connection_state.BestBlock() == previous_hash);
    BOOST_CHECK(!connection_state.GetCoin(coinbase_outpoint));
    {
        LOCK(::cs_main);
        BOOST_CHECK(!block_index.IsValid(BLOCK_VALID_SCRIPTS));
    }
    BOOST_CHECK(!undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, nullptr);

    CoreBlockConnectionCommitTarget commit_target{
        undo_writer,
        block_index_committer,
        connection_state,
        block_index};
    {
        LOCK(::cs_main);
        const auto commit_request{
            MakeBlockConnectionCommitRequest(commit_target, connection_state.Committer(), trace.trace, block, connection_state)};
        BOOST_REQUIRE(engine.Commit(commit_request, std::move(*validated.commit_package), state).Succeeded());
    }

    BOOST_CHECK(connection_state.BestBlock() == block_hash);
    const auto coinbase_coin{connection_state.GetCoin(coinbase_outpoint)};
    BOOST_REQUIRE(coinbase_coin);
    BOOST_CHECK_EQUAL(coinbase_coin->height, 1);
    BOOST_CHECK(coinbase_coin->is_coinbase);
    BOOST_CHECK_EQUAL(coinbase_coin->output.nValue, 50);
    {
        LOCK(::cs_main);
        BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    }
    BOOST_CHECK(undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, &block_index);
}

BOOST_AUTO_TEST_CASE(block_connection_commit_rejects_stale_parent)
{
    AssertLockNotHeld(::cs_main);

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
        .block_position = BlockPositionFor(block_index),
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.ConnectPrepared(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);

    connection_state.SetBestBlock(uint256{2});
    CoreBlockConnectionCommitTarget commit_target{
        undo_writer,
        block_index_committer,
        connection_state,
        block_index};
    {
        LOCK(::cs_main);
        const validation::BlockConnectionResult commit{engine.Commit(
            MakeBlockConnectionCommitRequest(commit_target, connection_state.Committer(), trace.trace, block, connection_state),
            std::move(*validated.commit_package),
            state)};
        BOOST_CHECK(!commit.Succeeded());
        BOOST_CHECK(!commit.commit_failure_state);
    }

    BOOST_CHECK(state.IsError());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "stale block connection");
    BOOST_CHECK(!undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, nullptr);
}

BOOST_AUTO_TEST_CASE(block_connection_engine_uses_explicit_spend_joiner)
{
    AssertLockNotHeld(::cs_main);

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

    FakeBlockSpendJoiner joiner;
    joiner.joined_inputs = Consensus::BlockSpentOutputJoin{
        .status = Consensus::BlockSpentOutputJoinStatus::MissingOrSpent,
        .failed_lookup = Consensus::BlockSpentOutputLookup{
            .outpoint = prevout,
            .transaction_index = 1,
            .input_index = 0,
        },
        .input_coins_by_transaction = {},
        .backend_mismatch = std::nullopt,
    };

    kernel::Notifications notifications;
    Consensus::DirectBlockScriptChecker script_checker;
    BlockConnectionTrace trace;

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
            .script_checker = script_checker,
            .trace = trace,
            .spend_joiner = &joiner,
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
        .block_position = BlockPositionFor(block_index),
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    const auto validated{validation::BlockConnectionEngine{}.ConnectPrepared(request, state)};

    BOOST_CHECK(!validated.Succeeded());
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-inputs-missingorspent");
}

BOOST_AUTO_TEST_CASE(block_connection_engine_validates_spends_with_snapshot_state_backend)
{
    AssertLockNotHeld(::cs_main);

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
    BlockConnectionTrace trace;

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
        .block_position = BlockPositionFor(block_index),
        .connection_state = connection_state,
        .options = {
            .block_check_options = Consensus::BlockCheckOptions{
                .check_pow = false,
            },
        },
    };

    BlockValidationState state;
    validation::BlockConnectionEngine engine;
    auto validated{engine.ConnectPrepared(request, state)};
    BOOST_REQUIRE(validated.Succeeded());
    BOOST_REQUIRE(validated.commit_package);
    CoreBlockConnectionCommitTarget commit_target{
        undo_writer,
        block_index_committer,
        connection_state,
        block_index};
    {
        LOCK(::cs_main);
        BOOST_REQUIRE(engine.Commit(MakeBlockConnectionCommitRequest(commit_target, connection_state.Committer(), trace, block, connection_state), std::move(*validated.commit_package), state).Succeeded());
    }

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
    BlockConnectionTrace trace;

    TestBlockValidityRequest request{
        .active_chain = active_chain,
        .consensus_params = consensus_params,
        .header_context = header_context,
        .connection_state = connection_state,
        .notifications = notifications,
        .script_checker = script_checker,
        .trace = trace,
        .sequence_lock_times = FixedSequenceLockTimes(),
    };

    const auto time{BlockValidationTime::FromUnixSeconds(block.nTime)};
    BOOST_REQUIRE(time);
    const BlockValidationState state{TestBlockValidity(
        request,
        block,
        Consensus::BlockCheckOptions{.check_pow = false},
        *time)};
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(connection_state.BestBlock() == previous_hash);
    BOOST_CHECK(!undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, nullptr);
}

BOOST_AUTO_TEST_CASE(block_storage_read_requests_snapshot_core_index_facts)
{
    LOCK(::cs_main);

    const uint256 parent_hash{};
    CBlockIndex parent;
    parent.phashBlock = &parent_hash;

    const uint256 block_hash{uint256::ONE};
    CBlockIndex block_index;
    block_index.phashBlock = &block_hash;
    block_index.pprev = &parent;
    block_index.nHeight = 42;
    block_index.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    block_index.nFile = 7;
    block_index.nDataPos = 123;
    block_index.nUndoPos = 456;

    const BlockDataReadRequest block_request{SnapshotBlockDataReadRequest(block_index)};
    BOOST_CHECK_EQUAL(block_request.position.nFile, 7);
    BOOST_CHECK_EQUAL(block_request.position.nPos, 123);
    BOOST_CHECK(block_request.expected_hash == block_hash);
    BOOST_CHECK_EQUAL(block_request.height, 42);

    const BlockUndoReadRequest undo_request{SnapshotBlockUndoReadRequest(block_index)};
    BOOST_CHECK_EQUAL(undo_request.position.nFile, 7);
    BOOST_CHECK_EQUAL(undo_request.position.nPos, 456);
    BOOST_CHECK(undo_request.block_hash == block_hash);
    BOOST_CHECK(undo_request.previous_block_hash == parent_hash);
    BOOST_CHECK_EQUAL(undo_request.height, 42);
}

BOOST_AUTO_TEST_CASE(replay_blocks_uses_copied_block_index_facts)
{
    LOCK(::cs_main);

    const uint256 block_hash{uint256::ONE};
    const BlockDataReadRequest read_request{
        .position = FlatFilePos{3, 99},
        .expected_hash = block_hash,
        .height = 1,
    };
    const BlockReplayBlock replay_block{
        .hash = block_hash,
        .previous_hash = uint256{},
        .height = 1,
        .block_read = read_request,
        .undo_read = std::nullopt,
    };
    FakeReplayIndex replay_index{replay_block};
    FakeReplayCoins replay_coins{{block_hash, uint256{}}};
    FakeReplayBlockReader block_reader;
    FakeReplayUndoReader undo_reader;
    kernel::Notifications notifications;

    const BlockReplayRequest request{
        .coins = replay_coins,
        .block_reader = block_reader,
        .undo_reader = undo_reader,
        .block_index = replay_index,
        .notifications = notifications,
    };

    BOOST_CHECK(ReplayBlocks(request));
    BOOST_REQUIRE_EQUAL(block_reader.requests.size(), 1);
    BOOST_CHECK(block_reader.requests.front().expected_hash == block_hash);
    BOOST_CHECK_EQUAL(block_reader.requests.front().position.nFile, 3);
    BOOST_CHECK_EQUAL(block_reader.requests.front().position.nPos, 99);
    BOOST_REQUIRE_EQUAL(replay_coins.rolled_forward.size(), 1);
    BOOST_CHECK(replay_coins.rolled_forward.front() == block_hash);
    BOOST_CHECK(replay_coins.best_block == block_hash);
    BOOST_CHECK(replay_coins.flushed);
}

BOOST_AUTO_TEST_CASE(verify_db_chain_returns_copied_block_facts)
{
    LOCK(::cs_main);

    const uint256 genesis_hash{};
    CBlockIndex genesis;
    genesis.phashBlock = &genesis_hash;
    genesis.nHeight = 0;

    const uint256 block_hash{uint256::ONE};
    CBlockIndex block;
    block.phashBlock = &block_hash;
    block.pprev = &genesis;
    block.nHeight = 1;
    block.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    block.nFile = 4;
    block.nDataPos = 11;
    block.nUndoPos = 22;

    CChain chain;
    chain.SetTip(block);
    CoreVerifyDBChain verify_chain{chain};

    const std::optional<VerifyDBBlock> tip{verify_chain.Tip()};
    BOOST_REQUIRE(tip);
    BOOST_CHECK(tip->replay.hash == block_hash);
    BOOST_CHECK_EQUAL(tip->replay.height, 1);
    BOOST_CHECK_EQUAL(tip->replay.block_read.position.nFile, 4);
    BOOST_CHECK_EQUAL(tip->replay.block_read.position.nPos, 11);
    BOOST_REQUIRE(tip->replay.undo_read);
    BOOST_CHECK_EQUAL(tip->replay.undo_read->position.nFile, 4);
    BOOST_CHECK_EQUAL(tip->replay.undo_read->position.nPos, 22);
    BOOST_CHECK_EQUAL(tip->status, BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO);

    const std::optional<VerifyDBBlock> previous{verify_chain.Previous(*tip)};
    BOOST_REQUIRE(previous);
    BOOST_CHECK(previous->replay.hash == genesis_hash);

    const std::optional<VerifyDBBlock> next{verify_chain.Next(*previous)};
    BOOST_REQUIRE(next);
    BOOST_CHECK(next->replay.hash == block_hash);
    BOOST_CHECK_EQUAL(verify_chain.CoreBlockIndexForConnection(*tip), &block);
}

BOOST_AUTO_TEST_SUITE_END()
