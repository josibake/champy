// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/predicates.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <ranges>

BOOST_AUTO_TEST_SUITE(consensus_predicates_tests)

BOOST_AUTO_TEST_CASE(coinbase_predicates_match_core_vocabulary)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vout.resize(1);

    const CTransaction tx{coinbase};
    BOOST_CHECK(Consensus::IsCoinbase(tx));
    BOOST_CHECK(Consensus::IsCoinbase(tx.vin[0].prevout));

    COutPoint outpoint;
    outpoint.n = 0;
    BOOST_CHECK(!Consensus::IsCoinbase(outpoint));
}

BOOST_AUTO_TEST_CASE(witness_predicate_matches_transaction_witness_state)
{
    CMutableTransaction without_witness;
    without_witness.vin.resize(1);
    without_witness.vout.resize(1);
    without_witness.vin[0].prevout.n = 0;

    BOOST_CHECK(!Consensus::HasWitness(CTransaction{without_witness}));

    CMutableTransaction with_witness{without_witness};
    with_witness.vin[0].scriptWitness.stack.emplace_back(32, 0x01);

    BOOST_CHECK(Consensus::HasWitness(CTransaction{with_witness}));
}

BOOST_AUTO_TEST_CASE(rbf_predicates_use_bip125_field_threshold)
{
    CTxIn input;

    input.nSequence = CTxIn::SEQUENCE_FINAL;
    BOOST_CHECK(!Consensus::SignalsRBF(input));

    input.nSequence = CTxIn::MAX_SEQUENCE_NONFINAL;
    BOOST_CHECK(!Consensus::SignalsRBF(input));

    input.nSequence = CTxIn::MAX_SEQUENCE_NONFINAL - 1;
    BOOST_CHECK(Consensus::SignalsRBF(input));

    CMutableTransaction tx;
    tx.vin.resize(2);
    tx.vout.resize(1);
    tx.vin[0].prevout.n = 0;
    tx.vin[1].prevout.n = 1;
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    tx.vin[1].nSequence = CTxIn::MAX_SEQUENCE_NONFINAL - 1;

    BOOST_CHECK(Consensus::SignalsRBF(CTransaction{tx}));
}

BOOST_AUTO_TEST_CASE(relative_locktime_predicate_is_field_only)
{
    CTxIn input;

    input.nSequence = CTxIn::SEQUENCE_FINAL;
    BOOST_CHECK(!Consensus::HasRelativeLocktime(input));

    input.nSequence = CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG;
    BOOST_CHECK(!Consensus::HasRelativeLocktime(input));

    input.nSequence = 0;
    BOOST_CHECK(Consensus::HasRelativeLocktime(input));

    input.nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(Consensus::HasRelativeLocktime(input));
    BOOST_CHECK(Consensus::RelativeLocktimeIsTime(input));

    input.nSequence = 1;
    BOOST_CHECK(Consensus::HasRelativeLocktime(input));
    BOOST_CHECK(!Consensus::RelativeLocktimeIsTime(input));
}

BOOST_AUTO_TEST_CASE(locktime_predicates_classify_height_and_time)
{
    BOOST_CHECK(Consensus::LocktimeIsHeight(LOCKTIME_THRESHOLD - 1));
    BOOST_CHECK(!Consensus::LocktimeIsTime(LOCKTIME_THRESHOLD - 1));

    BOOST_CHECK(!Consensus::LocktimeIsHeight(LOCKTIME_THRESHOLD));
    BOOST_CHECK(Consensus::LocktimeIsTime(LOCKTIME_THRESHOLD));

    CMutableTransaction tx;
    tx.nLockTime = LOCKTIME_THRESHOLD;
    BOOST_CHECK(Consensus::LocktimeIsTime(CTransaction{tx}));
}

BOOST_AUTO_TEST_CASE(script_predicates_wrap_script_byte_shape)
{
    const CScript empty;
    BOOST_CHECK(!Consensus::IsUnspendable(empty));

    const CScript op_return = CScript{} << OP_RETURN;
    BOOST_CHECK(Consensus::IsUnspendable(op_return));
    BOOST_CHECK(Consensus::IsUnspendable(Consensus::ScriptView{op_return}));

    const CTxOut txout{0, op_return};
    BOOST_CHECK(Consensus::IsUnspendable(txout));

    const std::vector<unsigned char> program(32, 0x01);
    const CScript p2wsh = CScript{} << OP_0 << program;

    const auto witness_program = Consensus::GetWitnessProgram(p2wsh);
    BOOST_REQUIRE(witness_program.has_value());
    BOOST_CHECK_EQUAL(witness_program->version, 0);
    BOOST_CHECK(witness_program->program == program);

    const auto witness_program_view = Consensus::GetWitnessProgramView(Consensus::ScriptView{p2wsh});
    BOOST_REQUIRE(witness_program_view.has_value());
    BOOST_CHECK_EQUAL(witness_program_view->version, 0);
    BOOST_CHECK(std::ranges::equal(witness_program_view->program, program));

    std::vector<unsigned char> p2sh_hash(20, 0x02);
    const CScript p2sh = CScript{} << OP_HASH160 << p2sh_hash << OP_EQUAL;
    BOOST_CHECK(Consensus::IsPayToScriptHash(p2sh));
    BOOST_CHECK(Consensus::IsPayToScriptHash(Consensus::ScriptView{p2sh}));

    BOOST_CHECK(!Consensus::GetWitnessProgram(op_return).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
