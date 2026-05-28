// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <chain.h>
#include <coins.h>
#include <block_coin_effects.h>
#include <consensus/block_spend.h>
#include <coins_view_spend_state.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <undo.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cassert>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

CTransactionRef MakeCoinbase(CAmount value = 0)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeSpendTx(const COutPoint& prevout, CAmount value, uint32_t sequence = CTxIn::SEQUENCE_FINAL)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.resize(1);
    tx.vin[0].prevout = prevout;
    tx.vin[0].nSequence = sequence;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeCoinbaseWithSigOps(int sigops)
{
    CMutableTransaction tx{*MakeCoinbase()};
    CScript script;
    for (int i{0}; i < sigops; ++i) {
        script << OP_CHECKSIG;
    }
    tx.vout[0].scriptPubKey = script;
    return MakeTransactionRef(tx);
}

Consensus::BlockSpendContext BlockSpendContext(int block_height = 2, int64_t previous_median_time_past = 0)
{
    return Consensus::BlockSpendContext{
        .block_height = block_height,
        .previous_median_time_past = previous_median_time_past,
    };
}

class FakeSequenceLockTimeView final : public Consensus::SequenceLockTimeView {
public:
    int64_t previous_median_time_past{0};
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint;

    int64_t PreviousMedianTimePast(const COutPoint& outpoint, int) const override
    {
        if (const auto configured{previous_median_time_past_by_outpoint.find(outpoint)}; configured != previous_median_time_past_by_outpoint.end()) {
            return configured->second;
        }
        return previous_median_time_past;
    }
};

const Consensus::SequenceLockTimeView& DefaultSequenceLockTimes()
{
    static const FakeSequenceLockTimeView view;
    return view;
}

Consensus::TransactionSpendContext SpendContext(
    int block_height = 2,
    int64_t previous_median_time_past = 0,
    const Consensus::SequenceLockTimeView& sequence_lock_times = DefaultSequenceLockTimes())
{
    return Consensus::TransactionSpendContext{
        .block_height = block_height,
        .previous_median_time_past = previous_median_time_past,
        .sequence_lock_times = sequence_lock_times,
    };
}

void CheckRejectReason(const Consensus::BlockSpendError& error, Consensus::BlockConsensusIssue issue, const std::string& reason)
{
    BOOST_CHECK(error.issue == issue);
    BOOST_CHECK_EQUAL(error.reject_reason, reason);
}

template <typename T>
void CheckRejectReason(const Consensus::BlockSpendResult<T>& check, Consensus::BlockConsensusIssue issue, const std::string& reason)
{
    BOOST_REQUIRE(!check);
    CheckRejectReason(check.error(), issue, reason);
}

Consensus::CoinsViewSpendState SpendState(const CCoinsViewCache& coins)
{
    return Consensus::CoinsViewSpendState{coins};
}

class FakeSpendState final : public Consensus::SpendStateView {
public:
    bool have_coin{false};
    COutPoint unspent_outpoint;
    CTxOut coin_output{0, CScript{} << OP_TRUE};
    int coin_height{0};
    bool coinbase{false};

    bool HaveCoin(const COutPoint& outpoint) const override
    {
        return have_coin && outpoint == unspent_outpoint;
    }

    std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const override
    {
        if (!HaveCoin(outpoint)) return std::nullopt;
        return Consensus::CoinSnapshot{
            .output = coin_output,
            .height = coin_height,
            .is_coinbase = coinbase,
        };
    }
};

class FakeBlockScriptChecker final : public Consensus::BlockScriptChecker {
public:
    bool wants_checks{true};
    bool fail{false};
    int checks{0};
    std::size_t last_input_count{0};
    Consensus::BlockSpendError error{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = "fake-script",
        .debug_message = "fake script failure",
    };

    bool WantsChecks() const override { return wants_checks; }

    Consensus::BlockSpendResult<void> Check(const Consensus::TransactionScriptCheckPlan& check) override
    {
        ++checks;
        last_input_count = check.spent_outputs.size();
        if (fail) return Consensus::Unexpected<Consensus::BlockSpendError>{error};
        return {};
    }

    Consensus::BlockSpendResult<void> Complete() override
    {
        return {};
    }
};

class FakeBlockSpendWorkspace final : public Consensus::BlockSpendWorkspace {
public:
    FakeSpendState view;
    FakeSequenceLockTimeView sequence_lock_times;
    int stages{0};
    std::optional<Consensus::BlockSpendError> stage_error;

    const Consensus::SpendStateView& StagedSpendView() const override { return view; }
    const Consensus::SequenceLockTimeView& SequenceLockTimes() const override { return sequence_lock_times; }

    Consensus::BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(const Consensus::TransactionCoinEffects& coin_effects, unsigned int transaction_index) override
    {
        ++stages;
        if (stage_error) return Consensus::Unexpected<Consensus::BlockSpendError>{*stage_error};
        if (transaction_index == 0) {
            BOOST_CHECK(coin_effects.spends.empty());
        }
        return {};
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_block_spend_tests)

BOOST_AUTO_TEST_CASE(spend_state_view_reads_coins_without_mutating)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/7, /*fCoinBase=*/false}, /*possible_overwrite=*/false);

    const Consensus::CoinsViewSpendState spend_state{coins};
    BOOST_CHECK(spend_state.HaveCoin(prevout));
    const auto coin{spend_state.GetCoin(prevout)};
    BOOST_REQUIRE(coin);
    BOOST_CHECK_EQUAL(coin->output.nValue, 50);
    BOOST_CHECK_EQUAL(coin->height, 7);
    BOOST_CHECK(!coin->is_coinbase);
    BOOST_CHECK(coins.HaveCoin(prevout));
}

BOOST_AUTO_TEST_CASE(sequence_lock_time_view_reads_coin_mtp_from_block_index)
{
    std::vector<CBlockIndex> blocks(20);
    for (int i{0}; i < static_cast<int>(blocks.size()); ++i) {
        blocks[i].nHeight = i;
        blocks[i].nTime = 1000 + i;
        blocks[i].pprev = i > 0 ? &blocks[i - 1] : nullptr;
        blocks[i].BuildSkip();
    }

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/12, /*fCoinBase=*/false}, /*possible_overwrite=*/false);

    const Consensus::CoinsViewSpendState spend_state{coins};
    const auto coin{spend_state.GetCoin(prevout)};
    BOOST_REQUIRE(coin);

    const Consensus::CoinsViewSequenceLockTimeView sequence_lock_times{blocks.back()};
    BOOST_CHECK_EQUAL(sequence_lock_times.PreviousMedianTimePast(prevout, coin->height), blocks[11].GetMedianTimePast());
}

BOOST_AUTO_TEST_CASE(coins_view_block_spend_workspace_stages_transaction_effects)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/1, /*fCoinBase=*/false}, /*possible_overwrite=*/false);

    Consensus::CoinsViewBlockSpendWorkspace workspace{coins, /*previous_median_time_past=*/0};
    const CTransactionRef tx{MakeSpendTx(prevout, 49)};
    const std::vector<Consensus::CoinSnapshot> input_coins{
        Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = false,
        },
    };
    const Consensus::TransactionCoinEffects effects{Consensus::BuildTransactionCoinEffectsForBlock(*tx, input_coins, /*block_height=*/2)};

    BOOST_CHECK(workspace.StageTransactionEffectsForIntraBlockView(effects, /*transaction_index=*/1));
    BOOST_CHECK(coins.HaveCoin(prevout));
    BOOST_CHECK(!workspace.StagedSpendView().HaveCoin(prevout));

    const auto created{workspace.StagedSpendView().GetCoin(COutPoint{tx->GetHash(), 0})};
    BOOST_REQUIRE(created);
    BOOST_CHECK_EQUAL(created->output.nValue, 49);
    BOOST_CHECK_EQUAL(created->height, 2);
    BOOST_CHECK(!created->is_coinbase);
}

BOOST_AUTO_TEST_CASE(block_no_unspent_output_overwrite_accepts_unknown_outputs)
{
    CBlock block;
    block.vtx = {MakeCoinbase()};
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};

    BOOST_CHECK(Consensus::CheckBlockNoUnspentOutputOverwrite(block.vtx, SpendState(coins)));
}

BOOST_AUTO_TEST_CASE(block_no_unspent_output_overwrite_rejects_unspent_duplicate)
{
    const CTransactionRef tx{MakeCoinbase()};
    CBlock block;
    block.vtx = {tx};

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    AddCoins(coins, *tx, /*nHeight=*/1);

    const auto result{Consensus::CheckBlockNoUnspentOutputOverwrite(block.vtx, SpendState(coins))};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-BIP30");
}

BOOST_AUTO_TEST_CASE(block_no_unspent_output_overwrite_accepts_spent_duplicate)
{
    const CTransactionRef tx{MakeCoinbase()};
    CBlock block;
    block.vtx = {tx};

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    AddCoins(coins, *tx, /*nHeight=*/1);
    BOOST_CHECK(coins.SpendCoin(COutPoint{tx->GetHash(), 0}));

    BOOST_CHECK(Consensus::CheckBlockNoUnspentOutputOverwrite(block.vtx, SpendState(coins)));
}

BOOST_AUTO_TEST_CASE(block_no_unspent_output_overwrite_uses_spend_state_interface)
{
    const CTransactionRef tx{MakeCoinbase()};
    CBlock block;
    block.vtx = {tx};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = COutPoint{tx->GetHash(), 0};

    const auto result{Consensus::CheckBlockNoUnspentOutputOverwrite(block.vtx, spend_state)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-BIP30");
}

BOOST_AUTO_TEST_CASE(block_coinbase_reward_check_rejects_excess_value)
{
    CBlock block;
    block.vtx = {MakeCoinbase(/*value=*/51)};

    const auto result{Consensus::CheckCoinbasePaysNoMoreThan(*block.vtx[0], /*max_reward=*/50)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-cb-amount");
}

BOOST_AUTO_TEST_CASE(block_coinbase_reward_check_accepts_value_at_limit)
{
    CBlock block;
    block.vtx = {MakeCoinbase(/*value=*/50)};

    BOOST_CHECK(Consensus::CheckCoinbasePaysNoMoreThan(*block.vtx[0], /*max_reward=*/50));
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_reports_missing_inputs_as_block_failure)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};

    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/1)};

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, SpendState(coins), SpendContext(/*block_height=*/1), /*locktime_flags=*/0)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-inputs-missingorspent");
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_checks_inputs_before_sequence_context)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40, CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1)};

    FakeSpendState spend_state;
    spend_state.unspent_outpoint = prevout;

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, spend_state, SpendContext(/*block_height=*/2), LOCKTIME_VERIFY_SEQUENCE)};
    BOOST_CHECK(!result);
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-inputs-missingorspent");
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_returns_fee_without_mutating_coins)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};

    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/1, /*fCoinBase=*/false}, /*possible_overwrite=*/false);
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, SpendState(coins), SpendContext(), /*locktime_flags=*/0)};
    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->fee, 10);
    BOOST_REQUIRE_EQUAL(result->input_coins.size(), 1);
    BOOST_CHECK_EQUAL(result->input_coins[0].output.nValue, 50);
    BOOST_CHECK_EQUAL(result->input_coins[0].height, 1);
    BOOST_CHECK(!result->input_coins[0].is_coinbase);
    BOOST_CHECK(coins.HaveCoin(prevout));
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_uses_spend_state_interface)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_height = 1;
    spend_state.coin_output = CTxOut{50, CScript{} << OP_TRUE};

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, spend_state, SpendContext(), /*locktime_flags=*/0)};
    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->fee, 10);
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_rejects_premature_coinbase_spend)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_output = CTxOut{50, CScript{} << OP_TRUE};
    spend_state.coin_height = 1;
    spend_state.coinbase = true;

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, spend_state, SpendContext(/*block_height=*/2), /*locktime_flags=*/0)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-premature-spend-of-coinbase");
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_rejects_input_value_out_of_range)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_output = CTxOut{MAX_MONEY + 1, CScript{} << OP_TRUE};
    spend_state.coin_height = 1;

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, spend_state, SpendContext(/*block_height=*/2), /*locktime_flags=*/0)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-inputvalues-outofrange");
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_rejects_value_in_below_outputs)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_output = CTxOut{39, CScript{} << OP_TRUE};
    spend_state.coin_height = 1;

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, spend_state, SpendContext(/*block_height=*/2), /*locktime_flags=*/0)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-in-belowout");
    BOOST_CHECK_EQUAL(result.error().debug_message, "value in (0.00000039) < value out (0.0000004) in transaction " + tx->GetHash().ToString());
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_rejects_nonfinal_sequence_locks)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};

    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/1, /*fCoinBase=*/false}, /*possible_overwrite=*/false);
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40, /*sequence=*/1)};

    const auto result{Consensus::CheckTransactionInputsForBlock(*tx, SpendState(coins), SpendContext(/*block_height=*/1), LOCKTIME_VERIFY_SEQUENCE)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-nonfinal");
}

BOOST_AUTO_TEST_CASE(transaction_inputs_for_block_uses_coin_sequence_time_context)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const uint32_t time_lock_sequence{CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40, time_lock_sequence)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_output = CTxOut{50, CScript{} << OP_TRUE};
    spend_state.coin_height = 1;

    FakeSequenceLockTimeView sequence_lock_times;
    sequence_lock_times.previous_median_time_past_by_outpoint.emplace(prevout, 400);
    BOOST_CHECK(Consensus::CheckTransactionInputsForBlock(
        *tx,
        spend_state,
        SpendContext(/*block_height=*/2, /*previous_median_time_past=*/1000, sequence_lock_times),
        LOCKTIME_VERIFY_SEQUENCE));

    sequence_lock_times.previous_median_time_past_by_outpoint.insert_or_assign(prevout, 600);
    const auto result{Consensus::CheckTransactionInputsForBlock(
        *tx,
        spend_state,
        SpendContext(/*block_height=*/2, /*previous_median_time_past=*/1000, sequence_lock_times),
        LOCKTIME_VERIFY_SEQUENCE)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-nonfinal");
}

BOOST_AUTO_TEST_CASE(transaction_spend_for_block_returns_accounting_and_coin_effects)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_output = CTxOut{50, CScript{} << OP_TRUE};
    spend_state.coin_height = 1;

    FakeBlockScriptChecker script_checker;
    const auto result{Consensus::ValidateTransactionSpendForBlock(
        tx,
        spend_state,
        script_checker,
        SpendContext(),
        Consensus::BlockSpendConsensusOptions{},
        Consensus::BlockSpendAccounting{.fees = 3, .sigop_cost = 0})};

    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->accounting.fees, 13);
    BOOST_CHECK_EQUAL(result->accounting.sigop_cost, 0);
    BOOST_REQUIRE_EQUAL(result->coin_effects.spends.size(), 1);
    BOOST_CHECK(result->coin_effects.spends[0].outpoint == prevout);
    BOOST_REQUIRE_EQUAL(result->coin_effects.creates.size(), 1);
    BOOST_CHECK_EQUAL(script_checker.checks, 1);
    BOOST_CHECK_EQUAL(script_checker.last_input_count, 1U);
}

BOOST_AUTO_TEST_CASE(transaction_spend_for_block_lets_script_checker_decline_work)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_output = CTxOut{50, CScript{} << OP_TRUE};
    spend_state.coin_height = 1;

    FakeBlockScriptChecker script_checker;
    script_checker.wants_checks = false;
    script_checker.fail = true;
    const auto result{Consensus::ValidateTransactionSpendForBlock(
        tx,
        spend_state,
        script_checker,
        SpendContext(),
        Consensus::BlockSpendConsensusOptions{},
        Consensus::BlockSpendAccounting{})};

    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(script_checker.checks, 0);
}

BOOST_AUTO_TEST_CASE(transaction_spend_for_block_returns_script_diagnostics)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};

    FakeSpendState spend_state;
    spend_state.have_coin = true;
    spend_state.unspent_outpoint = prevout;
    spend_state.coin_output = CTxOut{50, CScript{} << OP_TRUE};
    spend_state.coin_height = 1;

    FakeBlockScriptChecker script_checker;
    script_checker.fail = true;
    const auto result{Consensus::ValidateTransactionSpendForBlock(
        tx,
        spend_state,
        script_checker,
        SpendContext(),
        Consensus::BlockSpendConsensusOptions{},
        Consensus::BlockSpendAccounting{})};

    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "fake-script");
    BOOST_CHECK_EQUAL(script_checker.checks, 1);
}

BOOST_AUTO_TEST_CASE(block_spend_for_block_returns_effects)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef spend_tx{MakeSpendTx(prevout, /*value=*/40)};
    CBlock block;
    block.vtx = {MakeCoinbase(/*value=*/50), spend_tx};

    FakeBlockSpendWorkspace spend_state;
    spend_state.view.have_coin = true;
    spend_state.view.unspent_outpoint = prevout;
    spend_state.view.coin_output = CTxOut{50, CScript{} << OP_TRUE};
    spend_state.view.coin_height = 1;

    FakeBlockScriptChecker script_checker;
    const auto result{Consensus::ValidateAndStageBlockTransactions(
        block.vtx,
        spend_state,
        script_checker,
        BlockSpendContext(/*block_height=*/2, /*previous_median_time_past=*/0),
        Consensus::BlockSpendConsensusOptions{})};

    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->fees, 10);
    BOOST_CHECK_EQUAL(result->inputs, 2);
    BOOST_CHECK_EQUAL(result->sigop_cost, 0);
    BOOST_REQUIRE_EQUAL(result->transaction_effects.size(), 2);
    BOOST_REQUIRE_EQUAL(result->transaction_effects[1].spends.size(), 1);
    BOOST_CHECK_EQUAL(result->transaction_effects[1].spends[0].coin.output.nValue, 50);
    BOOST_CHECK_EQUAL(spend_state.stages, 2);
    BOOST_CHECK_EQUAL(script_checker.checks, 1);
}

BOOST_AUTO_TEST_CASE(block_spend_for_block_checks_unspent_output_overwrite_before_staging)
{
    CBlock block;
    block.vtx = {MakeCoinbase(/*value=*/50)};

    FakeBlockSpendWorkspace spend_state;
    spend_state.view.have_coin = true;
    spend_state.view.unspent_outpoint = COutPoint{block.vtx[0]->GetHash(), 0};

    FakeBlockScriptChecker script_checker;
    const auto result{Consensus::ValidateAndStageBlockTransactions(
        block.vtx,
        spend_state,
        script_checker,
        BlockSpendContext(),
        Consensus::BlockSpendConsensusOptions{
            .locktime_flags = 0,
            .script_flags = {},
            .check_no_unspent_output_overwrite = true,
        })};

    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-BIP30");
    BOOST_CHECK_EQUAL(spend_state.stages, 0);
    BOOST_CHECK_EQUAL(script_checker.checks, 0);
}

BOOST_AUTO_TEST_CASE(coins_view_block_spend_backend_creates_attempt_workspace)
{
    CBlock block;
    block.vtx = {MakeCoinbase(/*value=*/50)};

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    Consensus::CoinsViewBlockSpendBackend backend{coins};

    const auto workspace{backend.BeginBlockSpend(
        BlockSpendContext())};

    BOOST_REQUIRE(workspace);
    BOOST_CHECK((*workspace)->StagedSpendView().GetCoin(COutPoint{block.vtx[0]->GetHash(), 0}) == std::nullopt);
}

BOOST_AUTO_TEST_CASE(block_spend_for_block_returns_stage_diagnostics)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CTransactionRef spend_tx{MakeSpendTx(prevout, /*value=*/40)};
    CBlock block;
    block.vtx = {MakeCoinbase(/*value=*/50), spend_tx};

    FakeBlockSpendWorkspace spend_state;
    spend_state.view.have_coin = true;
    spend_state.view.unspent_outpoint = prevout;
    spend_state.view.coin_output = CTxOut{50, CScript{} << OP_TRUE};
    spend_state.view.coin_height = 1;
    spend_state.stage_error = Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = "fake-stage",
        .debug_message = "fake staging failure",
    };

    FakeBlockScriptChecker script_checker;
    const auto result{Consensus::ValidateAndStageBlockTransactions(
        block.vtx,
        spend_state,
        script_checker,
        BlockSpendContext(),
        Consensus::BlockSpendConsensusOptions{})};

    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "fake-stage");
    BOOST_CHECK_EQUAL(spend_state.stages, 1);
}

BOOST_AUTO_TEST_CASE(transaction_fee_for_block_accumulates_to_money_range_limit)
{
    const auto block_fees{Consensus::AddTransactionFeeForBlock(/*block_fees=*/MAX_MONEY - 1, /*tx_fee=*/1)};
    BOOST_REQUIRE(block_fees);
    BOOST_CHECK_EQUAL(*block_fees, MAX_MONEY);
}

BOOST_AUTO_TEST_CASE(transaction_fee_for_block_rejects_excess_accumulated_fee)
{
    const auto block_fees{Consensus::AddTransactionFeeForBlock(/*block_fees=*/MAX_MONEY, /*tx_fee=*/1)};
    CheckRejectReason(block_fees, Consensus::BlockConsensusIssue::Consensus, "bad-txns-accumulated-fee-outofrange");
}

BOOST_AUTO_TEST_CASE(transaction_sigop_cost_for_block_accumulates_legacy_sigops)
{
    const std::vector<Consensus::CoinSnapshot> input_coins;
    const CTransactionRef tx{MakeCoinbaseWithSigOps(/*sigops=*/1)};

    const auto block_sigop_cost{Consensus::AddTransactionSigOpCostForBlock(*tx, input_coins, /*flags=*/0, /*block_sigop_cost=*/0)};
    BOOST_REQUIRE(block_sigop_cost);
    BOOST_CHECK_EQUAL(*block_sigop_cost, WITNESS_SCALE_FACTOR);
}

BOOST_AUTO_TEST_CASE(transaction_sigop_cost_for_block_counts_p2sh_from_input_snapshot)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const CScript redeem_script{CScript{} << OP_CHECKSIG};
    const std::vector<Consensus::CoinSnapshot> input_coins{
        Consensus::CoinSnapshot{
            .output = CTxOut{50, GetScriptForDestination(ScriptHash(redeem_script))},
            .height = 1,
            .is_coinbase = false,
        },
    };

    CMutableTransaction mutable_tx{*MakeSpendTx(prevout, /*value=*/40)};
    mutable_tx.vin[0].scriptSig = CScript{} << ToByteVector(redeem_script);
    const CTransactionRef tx{MakeTransactionRef(mutable_tx)};

    const auto block_sigop_cost{Consensus::AddTransactionSigOpCostForBlock(*tx, input_coins, SCRIPT_VERIFY_P2SH, /*block_sigop_cost=*/0)};
    BOOST_REQUIRE(block_sigop_cost);
    BOOST_CHECK_EQUAL(*block_sigop_cost, WITNESS_SCALE_FACTOR);
}

BOOST_AUTO_TEST_CASE(transaction_sigop_cost_for_block_rejects_excess_total)
{
    const std::vector<Consensus::CoinSnapshot> input_coins;
    const CTransactionRef tx{MakeCoinbaseWithSigOps(/*sigops=*/1)};

    const auto block_sigop_cost{Consensus::AddTransactionSigOpCostForBlock(*tx, input_coins, /*flags=*/0, /*block_sigop_cost=*/MAX_BLOCK_SIGOPS_COST)};
    CheckRejectReason(block_sigop_cost, Consensus::BlockConsensusIssue::Consensus, "bad-blk-sigops");
}

BOOST_AUTO_TEST_CASE(transaction_coin_effects_for_block_build_spend_and_create_values)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const std::vector<Consensus::CoinSnapshot> input_coins{
        Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = false,
        },
    };

    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};
    const auto effects{Consensus::BuildTransactionCoinEffectsForBlock(*tx, input_coins, /*block_height=*/2)};

    BOOST_REQUIRE_EQUAL(effects.spends.size(), 1);
    BOOST_CHECK(effects.spends[0].outpoint == prevout);
    BOOST_CHECK_EQUAL(effects.spends[0].coin.output.nValue, 50);
    BOOST_CHECK_EQUAL(effects.spends[0].coin.height, 1);
    BOOST_CHECK(!effects.spends[0].coin.is_coinbase);

    BOOST_REQUIRE_EQUAL(effects.creates.size(), 1);
    const COutPoint created_outpoint{tx->GetHash(), 0};
    BOOST_CHECK(effects.creates[0].outpoint == created_outpoint);
    BOOST_CHECK_EQUAL(effects.creates[0].coin.output.nValue, 40);
    BOOST_CHECK_EQUAL(effects.creates[0].coin.height, 2);
    BOOST_CHECK(!effects.creates[0].coin.is_coinbase);
}

BOOST_AUTO_TEST_CASE(transaction_coin_effects_for_block_build_coinbase_create)
{
    const CTransactionRef tx{MakeCoinbase(/*value=*/25)};

    const auto effects{Consensus::BuildTransactionCoinEffectsForBlock(*tx, {}, /*block_height=*/3)};

    BOOST_CHECK(effects.spends.empty());
    BOOST_REQUIRE_EQUAL(effects.creates.size(), 1);
    const COutPoint created_outpoint{tx->GetHash(), 0};
    BOOST_CHECK(effects.creates[0].outpoint == created_outpoint);
    BOOST_CHECK_EQUAL(effects.creates[0].coin.output.nValue, 25);
    BOOST_CHECK_EQUAL(effects.creates[0].coin.height, 3);
    BOOST_CHECK(effects.creates[0].coin.is_coinbase);
}

BOOST_AUTO_TEST_CASE(transaction_coin_effects_for_block_apply_values_to_cache_and_undo)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/1, /*fCoinBase=*/false}, /*possible_overwrite=*/false);

    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};
    const std::vector<Consensus::CoinSnapshot> input_coins{
        Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = false,
        },
    };
    const auto effects{Consensus::BuildTransactionCoinEffectsForBlock(*tx, input_coins, /*block_height=*/2)};

    CTxUndo undo;
    Consensus::ApplyTransactionCoinEffectsForBlock(effects, coins, undo);

    BOOST_CHECK(!coins.HaveCoin(prevout));
    BOOST_REQUIRE_EQUAL(undo.vprevout.size(), 1);
    BOOST_CHECK_EQUAL(undo.vprevout[0].out.nValue, 50);
    BOOST_CHECK_EQUAL(undo.vprevout[0].nHeight, 1U);

    const COutPoint created_outpoint{tx->GetHash(), 0};
    const auto staged_coin{coins.GetCoin(created_outpoint)};
    BOOST_REQUIRE(staged_coin.has_value());
    BOOST_CHECK_EQUAL(staged_coin->out.nValue, 40);
    BOOST_CHECK_EQUAL(staged_coin->nHeight, 2U);
    BOOST_CHECK(!staged_coin->IsCoinBase());
}

BOOST_AUTO_TEST_CASE(transaction_coin_effects_for_block_stage_spend_and_undo)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/1, /*fCoinBase=*/false}, /*possible_overwrite=*/false);

    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};
    CTxUndo undo;
    Consensus::StageTransactionCoinsForBlock(*tx, coins, undo, /*block_height=*/2);

    BOOST_CHECK(!coins.HaveCoin(prevout));
    BOOST_REQUIRE_EQUAL(undo.vprevout.size(), 1);
    BOOST_CHECK_EQUAL(undo.vprevout[0].out.nValue, 50);
    BOOST_CHECK_EQUAL(undo.vprevout[0].nHeight, 1U);

    const auto staged_coin{coins.GetCoin(COutPoint{tx->GetHash(), 0})};
    BOOST_REQUIRE(staged_coin.has_value());
    BOOST_CHECK_EQUAL(staged_coin->out.nValue, 40);
    BOOST_CHECK_EQUAL(staged_coin->nHeight, 2U);
    BOOST_CHECK(!staged_coin->IsCoinBase());
}

BOOST_AUTO_TEST_CASE(staged_coin_effects_for_block_commit_to_parent_view)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    const uint256 best_block{uint256::ONE};
    coins.SetBestBlock(best_block);

    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    coins.AddCoin(prevout, Coin{CTxOut{50, CScript{} << OP_TRUE}, /*nHeight=*/1, /*fCoinBase=*/false}, /*possible_overwrite=*/false);

    CCoinsViewCache staged_coins{&coins};
    const CTransactionRef tx{MakeSpendTx(prevout, /*value=*/40)};
    CTxUndo undo;
    Consensus::StageTransactionCoinsForBlock(*tx, staged_coins, undo, /*block_height=*/2);

    const COutPoint new_output{tx->GetHash(), 0};
    BOOST_CHECK(coins.HaveCoin(prevout));
    BOOST_CHECK(!coins.HaveCoin(new_output));
    BOOST_CHECK(!staged_coins.HaveCoin(prevout));
    BOOST_CHECK(staged_coins.HaveCoin(new_output));

    Consensus::CommitStagedCoinsForBlock(staged_coins, coins);

    BOOST_CHECK(!coins.HaveCoin(prevout));
    const auto committed_coin{coins.GetCoin(new_output)};
    BOOST_REQUIRE(committed_coin.has_value());
    BOOST_CHECK_EQUAL(committed_coin->out.nValue, 40);
    BOOST_CHECK_EQUAL(committed_coin->nHeight, 2U);
    BOOST_CHECK_EQUAL(coins.GetBestBlock().ToString(), best_block.ToString());
}

BOOST_AUTO_TEST_CASE(transaction_coin_effects_for_block_stage_coinbase_without_undo)
{
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    const CTransactionRef tx{MakeCoinbase(/*value=*/25)};
    CTxUndo undo;

    Consensus::StageTransactionCoinsForBlock(*tx, coins, undo, /*block_height=*/3);

    BOOST_CHECK(undo.vprevout.empty());
    const auto staged_coin{coins.GetCoin(COutPoint{tx->GetHash(), 0})};
    BOOST_REQUIRE(staged_coin.has_value());
    BOOST_CHECK_EQUAL(staged_coin->out.nValue, 25);
    BOOST_CHECK_EQUAL(staged_coin->nHeight, 3U);
    BOOST_CHECK(staged_coin->IsCoinBase());
}

BOOST_AUTO_TEST_SUITE_END()
