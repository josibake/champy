// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/script_checker.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_check.h>
#include <script/sigcache.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

CTransactionRef MakeSpend()
{
    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    tx.vout.emplace_back(49, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

Consensus::CoinSnapshot Coin(CScript script)
{
    return Consensus::CoinSnapshot{
        .output = CTxOut{50, std::move(script)},
        .height = 1,
        .is_coinbase = false,
    };
}

void CheckRejectReason(const Consensus::BlockSpendResult<void>& result, const std::string& reason)
{
    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, reason);
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_script_checker_tests)

BOOST_AUTO_TEST_CASE(script_check_plan_uses_input_snapshots)
{
    const CTransactionRef tx{MakeSpend()};
    std::vector<Consensus::CoinSnapshot> input_coins{Coin(CScript{} << OP_TRUE)};

    const auto check{Consensus::BuildTransactionScriptCheckPlan(tx, input_coins, SCRIPT_VERIFY_P2SH)};

    BOOST_CHECK(check.tx == tx);
    BOOST_CHECK(check.flags == SCRIPT_VERIFY_P2SH);
    BOOST_REQUIRE_EQUAL(check.spent_outputs.size(), 1U);
    BOOST_CHECK_EQUAL(check.spent_outputs[0].nValue, 50);
    BOOST_CHECK(check.spent_outputs[0].scriptPubKey == (CScript{} << OP_TRUE));
}

BOOST_AUTO_TEST_CASE(direct_script_checker_accepts_true_script)
{
    const CTransactionRef tx{MakeSpend()};
    std::vector<Consensus::CoinSnapshot> input_coins{Coin(CScript{} << OP_TRUE)};
    const auto check{Consensus::BuildTransactionScriptCheckPlan(tx, input_coins, script_verify_flags{})};
    Consensus::DirectBlockScriptChecker checker;

    BOOST_CHECK(checker.Check(check));
    BOOST_CHECK(checker.Complete());
}

BOOST_AUTO_TEST_CASE(direct_script_checker_returns_script_diagnostics)
{
    const CTransactionRef tx{MakeSpend()};
    std::vector<Consensus::CoinSnapshot> input_coins{Coin(CScript{} << OP_FALSE)};
    const auto check{Consensus::BuildTransactionScriptCheckPlan(tx, input_coins, script_verify_flags{})};
    Consensus::DirectBlockScriptChecker checker;

    CheckRejectReason(
        checker.Check(check),
        "block-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack element)");
}

BOOST_AUTO_TEST_CASE(script_check_owns_queued_block_inputs)
{
    CTransactionRef tx{MakeSpend()};
    auto txdata{std::make_shared<PrecomputedTransactionData>()};
    txdata->Init(*tx, {CTxOut{50, CScript{} << OP_TRUE}});
    SignatureCache signature_cache{DEFAULT_SIGNATURE_CACHE_BYTES};

    CScriptCheck check{
        CTxOut{50, CScript{} << OP_TRUE},
        tx,
        signature_cache,
        /*nInIn=*/0,
        script_verify_flags{},
        /*cacheIn=*/false,
        txdata};

    tx.reset();
    txdata.reset();

    BOOST_CHECK(!check().has_value());
}

BOOST_AUTO_TEST_CASE(script_check_owning_move_keeps_inputs_alive)
{
    CTransactionRef tx{MakeSpend()};
    auto txdata{std::make_shared<PrecomputedTransactionData>()};
    txdata->Init(*tx, {CTxOut{50, CScript{} << OP_TRUE}});
    SignatureCache signature_cache{DEFAULT_SIGNATURE_CACHE_BYTES};

    std::vector<CScriptCheck> checks;
    checks.reserve(1);
    checks.emplace_back(
        CTxOut{50, CScript{} << OP_TRUE},
        tx,
        signature_cache,
        /*nInIn=*/0,
        script_verify_flags{},
        /*cacheIn=*/false,
        txdata);
    checks.emplace_back(
        CTxOut{50, CScript{} << OP_TRUE},
        MakeSpend(),
        signature_cache,
        /*nInIn=*/0,
        script_verify_flags{},
        /*cacheIn=*/false,
        std::make_shared<PrecomputedTransactionData>(*txdata));

    tx.reset();
    txdata.reset();

    BOOST_CHECK(!checks.front()().has_value());
}

BOOST_AUTO_TEST_SUITE_END()
