// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <consensus/block_check.h>
#include <consensus/block_facts.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

namespace {

CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeCoinbaseWithScriptSig(const CScript& script_sig)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = script_sig;
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CScript WitnessCommitmentScript(const uint256& commitment)
{
    std::vector<unsigned char> commitment_data{0xaa, 0x21, 0xa9, 0xed};
    commitment_data.insert(commitment_data.end(), commitment.begin(), commitment.end());
    return CScript{} << OP_RETURN << commitment_data;
}

CTransactionRef MakeRegularTx(uint32_t output_index = 0)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.n = output_index;
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeWitnessTx()
{
    CMutableTransaction tx{*MakeRegularTx()};
    tx.vin[0].scriptWitness.stack.emplace_back(32, 0x01);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeDuplicateInputTx()
{
    CMutableTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout.n = 1;
    tx.vin[1].prevout.n = 1;
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeLockedTx(uint32_t locktime, uint32_t sequence)
{
    CMutableTransaction tx{*MakeRegularTx()};
    tx.nLockTime = locktime;
    tx.vin[0].nSequence = sequence;
    return MakeTransactionRef(tx);
}

CTransactionRef MakeHighSigOpsTx()
{
    CMutableTransaction tx{*MakeRegularTx()};
    CScript sigops_script;
    for (int i{0}; i <= MAX_BLOCK_SIGOPS_COST / WITNESS_SCALE_FACTOR; ++i) {
        sigops_script << OP_CHECKSIG;
    }
    tx.vout[0].scriptPubKey = sigops_script;
    return MakeTransactionRef(tx);
}

template <typename T>
void CheckRejectReason(const Consensus::BlockCheckResult<T>& check, Consensus::BlockConsensusIssue issue, const std::string& reason)
{
    BOOST_REQUIRE(!check);
    BOOST_CHECK(check.error().issue == issue);
    BOOST_CHECK_EQUAL(check.error().reject_reason, reason);
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_block_check_tests)

BOOST_AUTO_TEST_CASE(block_header_check_respects_pow_flag)
{
    Consensus::Params params;
    params.powLimit = uint256::ONE;

    CBlockHeader header;
    header.nBits = UintToArith256(params.powLimit).GetCompact();

    BOOST_CHECK(Consensus::CheckBlockHeader(header, params, {.check_pow = false}));

    const auto checked_result{Consensus::CheckBlockHeader(header, params, {.check_pow = true})};
    CheckRejectReason(checked_result, Consensus::BlockConsensusIssue::InvalidHeader, "high-hash");
}

BOOST_AUTO_TEST_CASE(block_header_previous_median_time_check_rejects_old_timestamp)
{
    CBlockHeader header;
    header.nTime = 100;

    const auto stale_check{Consensus::CheckBlockPreviousMedianTime(header, /*previous_median_time_past=*/100)};
    CheckRejectReason(stale_check, Consensus::BlockConsensusIssue::InvalidHeader, "time-too-old");

    header.nTime = 101;
    BOOST_CHECK(Consensus::CheckBlockPreviousMedianTime(header, /*previous_median_time_past=*/100));
}

BOOST_AUTO_TEST_CASE(block_header_timewarp_check_applies_to_adjustment_boundaries)
{
    CBlockHeader header;
    header.nTime = 1000 - MAX_TIMEWARP - 1;

    BOOST_CHECK(Consensus::CheckBlockTimewarp(header, {
        .block_height = 2016,
        .difficulty_adjustment_interval = 2016,
        .previous_block_time = 1000,
        .enforce_timewarp_protection = false}));

    BOOST_CHECK(Consensus::CheckBlockTimewarp(header, {
        .block_height = 2015,
        .difficulty_adjustment_interval = 2016,
        .previous_block_time = 1000,
        .enforce_timewarp_protection = true}));

    const auto active_check{Consensus::CheckBlockTimewarp(header, {
        .block_height = 2016,
        .difficulty_adjustment_interval = 2016,
        .previous_block_time = 1000,
        .enforce_timewarp_protection = true})};
    CheckRejectReason(active_check, Consensus::BlockConsensusIssue::InvalidHeader, "time-timewarp-attack");

    header.nTime = 1000 - MAX_TIMEWARP;
    BOOST_CHECK(Consensus::CheckBlockTimewarp(header, {
        .block_height = 2016,
        .difficulty_adjustment_interval = 2016,
        .previous_block_time = 1000,
        .enforce_timewarp_protection = true}));
}

BOOST_AUTO_TEST_CASE(block_header_future_time_check_uses_explicit_limit)
{
    CBlockHeader header;
    header.nTime = 1001;

    const auto future_check{Consensus::CheckBlockFutureTime(header, /*max_block_time=*/1000)};
    CheckRejectReason(future_check, Consensus::BlockConsensusIssue::TimeFuture, "time-too-new");

    header.nTime = 1000;
    BOOST_CHECK(Consensus::CheckBlockFutureTime(header, /*max_block_time=*/1000));
}

BOOST_AUTO_TEST_CASE(block_contextual_header_rules_preserve_check_order)
{
    Consensus::BlockContextualHeaderOptions options;
    options.block_height = 100;
    options.difficulty_adjustment_interval = 2016;
    options.previous_median_time_past = 100;
    options.previous_block_time = 100;
    options.max_block_time = 200;
    options.height_in_coinbase_active = true;

    CBlockHeader header;
    header.nTime = 100;
    header.nVersion = 1;

    const auto time_check{Consensus::CheckBlockContextualHeaderRules(header, options)};
    CheckRejectReason(time_check, Consensus::BlockConsensusIssue::InvalidHeader, "time-too-old");

    header.nTime = 101;
    const auto version_check{Consensus::CheckBlockContextualHeaderRules(header, options)};
    CheckRejectReason(version_check, Consensus::BlockConsensusIssue::InvalidHeader, "bad-version(0x00000001)");

    header.nVersion = 2;
    BOOST_CHECK(Consensus::CheckBlockContextualHeaderRules(header, options));
}

BOOST_AUTO_TEST_CASE(block_header_admission_rules_check_difficulty_before_context)
{
    Consensus::BlockHeaderAdmissionOptions options;
    options.expected_difficulty_bits = 11;
    options.contextual.block_height = 100;
    options.contextual.difficulty_adjustment_interval = 2016;
    options.contextual.previous_median_time_past = 100;
    options.contextual.previous_block_time = 100;
    options.contextual.max_block_time = 200;
    options.contextual.height_in_coinbase_active = true;

    CBlockHeader header;
    header.nBits = 10;
    header.nTime = 100;
    header.nVersion = 1;

    const auto difficulty_check{Consensus::CheckBlockHeaderAdmissionRules(header, options)};
    CheckRejectReason(difficulty_check, Consensus::BlockConsensusIssue::InvalidHeader, "bad-diffbits");

    header.nBits = 11;
    const auto time_check{Consensus::CheckBlockHeaderAdmissionRules(header, options)};
    CheckRejectReason(time_check, Consensus::BlockConsensusIssue::InvalidHeader, "time-too-old");

    header.nTime = 101;
    const auto version_check{Consensus::CheckBlockHeaderAdmissionRules(header, options)};
    CheckRejectReason(version_check, Consensus::BlockConsensusIssue::InvalidHeader, "bad-version(0x00000001)");

    header.nVersion = 2;
    BOOST_CHECK(Consensus::CheckBlockHeaderAdmissionRules(header, options));
}

BOOST_AUTO_TEST_CASE(block_contextual_header_options_use_header_context)
{
    const Consensus::BlockHeaderContext headers{
        2016,
        100,
        95,
        Consensus::BlockDeploymentContext{
            .height_in_coinbase_active = true,
            .der_signature_active = true,
        },
    };

    Consensus::Params params;
    params.nPowTargetSpacing = 10;
    params.nPowTargetTimespan = 100;
    params.enforce_BIP94 = true;

    const auto options{Consensus::BuildBlockContextualHeaderOptions(headers, params, /*max_block_time=*/200)};

    BOOST_CHECK_EQUAL(options.block_height, 2016);
    BOOST_CHECK_EQUAL(options.difficulty_adjustment_interval, 10);
    BOOST_CHECK_EQUAL(options.previous_median_time_past, 100);
    BOOST_CHECK_EQUAL(options.previous_block_time, 95);
    BOOST_CHECK_EQUAL(options.max_block_time, 200);
    BOOST_CHECK(options.enforce_timewarp_protection);
    BOOST_CHECK(options.height_in_coinbase_active);
    BOOST_CHECK(options.der_signature_active);
    BOOST_CHECK(!options.cltv_active);
}

BOOST_AUTO_TEST_CASE(block_structural_rules_compose_header_merkle_and_body_checks)
{
    Consensus::Params params;
    params.powLimit = uint256::ONE;

    CBlock block;
    block.nBits = UintToArith256(params.powLimit).GetCompact();
    block.vtx = {MakeCoinbase()};

    Consensus::BlockCheckOptions options;
    options.check_pow = false;
    options.check_merkle_root = false;

    BOOST_CHECK(Consensus::CheckBlockStructuralRules(block, params, options));

    options.check_merkle_root = true;
    const auto merkle_check{Consensus::CheckBlockStructuralRules(block, params, options)};
    CheckRejectReason(merkle_check, Consensus::BlockConsensusIssue::Mutated, "bad-txnmrklroot");

    block.hashMerkleRoot = BlockMerkleRoot(block);
    options.check_pow = true;
    const auto pow_check{Consensus::CheckBlockStructuralRules(block, params, options)};
    CheckRejectReason(pow_check, Consensus::BlockConsensusIssue::InvalidHeader, "high-hash");
}

BOOST_AUTO_TEST_CASE(block_version_check_uses_active_deployment_thresholds)
{
    CBlockHeader header;
    header.nVersion = 1;

    BOOST_CHECK(Consensus::CheckBlockVersion(header, {
        .height_in_coinbase_active = false,
        .der_signature_active = false,
        .cltv_active = false}));

    const auto active_check{Consensus::CheckBlockVersion(header, {
        .height_in_coinbase_active = true,
        .der_signature_active = false,
        .cltv_active = false})};
    CheckRejectReason(active_check, Consensus::BlockConsensusIssue::InvalidHeader, "bad-version(0x00000001)");
}

BOOST_AUTO_TEST_CASE(block_merkle_check_rejects_header_mismatch)
{
    CBlock block;
    block.vtx = {MakeCoinbase()};

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    const auto result{Consensus::CheckBlockMerkleRoot(block, facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Mutated, "bad-txnmrklroot");
}

BOOST_AUTO_TEST_CASE(block_merkle_check_rejects_duplicate_tree_mutation)
{
    CBlock block;
    const auto tx{MakeRegularTx()};
    block.vtx = {tx, tx};
    block.hashMerkleRoot = BlockMerkleRoot(block);

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    const auto result{Consensus::CheckBlockMerkleRoot(block, facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Mutated, "bad-txns-duplicate");
}

BOOST_AUTO_TEST_CASE(block_body_rejects_empty_block)
{
    const CBlock block;
    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    const auto result{Consensus::CheckBlockBody(block, facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-blk-length");
}

BOOST_AUTO_TEST_CASE(block_body_rejects_missing_coinbase)
{
    CBlock block;
    block.vtx = {MakeRegularTx()};

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    const auto result{Consensus::CheckBlockBody(block, facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-cb-missing");
}

BOOST_AUTO_TEST_CASE(block_body_rejects_multiple_coinbases)
{
    CBlock block;
    block.vtx = {MakeCoinbase(), MakeCoinbase()};

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    const auto result{Consensus::CheckBlockBody(block, facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-cb-multiple");
}

BOOST_AUTO_TEST_CASE(block_body_reports_transaction_failure)
{
    CBlock block;
    block.vtx = {MakeCoinbase(), MakeDuplicateInputTx()};

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    const auto result{Consensus::CheckBlockBody(block, facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-inputs-duplicate");
}

BOOST_AUTO_TEST_CASE(block_body_rejects_excess_legacy_sigops)
{
    CBlock block;
    block.vtx = {MakeCoinbase(), MakeHighSigOpsTx()};

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    const auto result{Consensus::CheckBlockBody(block, facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-blk-sigops");
}

BOOST_AUTO_TEST_CASE(block_body_accepts_minimal_valid_shape)
{
    CBlock block;
    block.vtx = {MakeCoinbase(), MakeRegularTx()};

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    BOOST_CHECK(Consensus::CheckBlockBody(block, facts));
}

BOOST_AUTO_TEST_CASE(block_transaction_span_checks_accept_minimal_valid_shape)
{
    CBlock block;
    block.vtx = {MakeCoinbaseWithScriptSig(CScript{} << 100), MakeRegularTx()};

    const auto structural_facts{Consensus::ComputeBlockStructuralFacts(block)};
    BOOST_CHECK(Consensus::CheckBlockTransactions(block.vtx, structural_facts));

    Consensus::BlockContextualTransactionOptions options;
    options.block_height = 100;
    options.locktime_cutoff = 0;
    options.enforce_coinbase_height = true;
    BOOST_CHECK(Consensus::CheckBlockContextualTransactionRules(block.vtx, options));

    const auto facts{Consensus::ComputeBlockFacts(block)};
    BOOST_CHECK(Consensus::CheckBlockWitnessRules(block.vtx, facts, {
        .expect_witness_commitment = false,
        .debug_context = "test-context"}));
}

BOOST_AUTO_TEST_CASE(block_transaction_span_rejects_mismatched_structural_facts)
{
    CBlock block;
    block.vtx = {MakeCoinbase(), MakeRegularTx()};

    auto structural_facts{Consensus::ComputeBlockStructuralFacts(block)};
    ++structural_facts.transaction_count;

    const auto result{Consensus::CheckBlockTransactions(block.vtx, structural_facts)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-validation-view");
}

BOOST_AUTO_TEST_CASE(block_witness_check_accepts_valid_commitment)
{
    const std::vector<unsigned char> nonce(32, 0x00);

    CMutableTransaction coinbase{*MakeCoinbase()};
    coinbase.vin[0].scriptWitness.stack.push_back(nonce);

    CBlock block;
    block.vtx = {MakeTransactionRef(coinbase), MakeWitnessTx()};

    uint256 commitment{BlockWitnessMerkleRoot(block)};
    CHash256().Write(commitment).Write(nonce).Finalize(commitment);

    coinbase.vout[0].scriptPubKey = WitnessCommitmentScript(commitment);
    block.vtx[0] = MakeTransactionRef(coinbase);

    const auto facts{Consensus::ComputeBlockFacts(block)};

    BOOST_CHECK(Consensus::CheckBlockWitnessMalleation(block, facts, {.expect_witness_commitment = true}));
}

BOOST_AUTO_TEST_CASE(block_witness_check_rejects_bad_coinbase_nonce)
{
    CMutableTransaction coinbase{*MakeCoinbase()};
    coinbase.vin[0].scriptWitness.stack.emplace_back(31, 0x00);
    coinbase.vout[0].scriptPubKey = WitnessCommitmentScript(uint256::ZERO);

    CBlock block;
    block.vtx = {MakeTransactionRef(coinbase), MakeWitnessTx()};

    const auto facts{Consensus::ComputeBlockFacts(block)};

    const auto result{Consensus::CheckBlockWitnessMalleation(block, facts, {.expect_witness_commitment = true})};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Mutated, "bad-witness-nonce-size");
}

BOOST_AUTO_TEST_CASE(block_witness_check_rejects_uncommitted_witness)
{
    CBlock block;
    block.vtx = {MakeCoinbase(), MakeWitnessTx()};

    const auto facts{Consensus::ComputeBlockFacts(block)};

    const auto result{Consensus::CheckBlockWitnessMalleation(block, facts, {.expect_witness_commitment = false})};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Mutated, "unexpected-witness");
}

BOOST_AUTO_TEST_CASE(block_witness_check_rejects_invalid_commitment_index)
{
    CBlock block;
    block.vtx = {MakeCoinbase()};

    auto facts{Consensus::ComputeBlockFacts(block)};
    facts.witness_commitment_index = 1;

    const auto result{Consensus::CheckBlockWitnessMalleation(block, facts, {.expect_witness_commitment = true})};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-validation-view");
}

BOOST_AUTO_TEST_CASE(block_weight_check_rejects_excess_weight)
{
    Consensus::BlockFacts facts;
    facts.weight = MAX_BLOCK_WEIGHT + 1;

    const auto result{Consensus::CheckBlockWeight(facts, "test-context")};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-blk-weight");
    BOOST_CHECK_EQUAL(result.error().debug_message, "test-context : weight limit failed");
}

BOOST_AUTO_TEST_CASE(block_witness_rules_check_weight_after_witness_shape)
{
    CBlock block;
    block.vtx = {MakeCoinbase()};

    auto facts{Consensus::ComputeBlockFacts(block)};
    facts.weight = MAX_BLOCK_WEIGHT + 1;

    const auto result{Consensus::CheckBlockWitnessRules(block, facts, {
        .expect_witness_commitment = false,
        .debug_context = "test-context"})};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-blk-weight");
}

BOOST_AUTO_TEST_CASE(block_final_transactions_rejects_nonfinal_transaction)
{
    CBlock block;
    block.vtx = {MakeLockedTx(/*locktime=*/100, /*sequence=*/0)};

    const auto result{Consensus::CheckBlockFinalTransactions(block, /*block_height=*/100, /*locktime_cutoff=*/0)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-txns-nonfinal");
}

BOOST_AUTO_TEST_CASE(block_final_transactions_accepts_final_transactions)
{
    CBlock block;
    block.vtx = {
        MakeLockedTx(/*locktime=*/100, /*sequence=*/0),
        MakeLockedTx(/*locktime=*/200, CTxIn::SEQUENCE_FINAL),
    };

    BOOST_CHECK(Consensus::CheckBlockFinalTransactions(block, /*block_height=*/101, /*locktime_cutoff=*/0));
}

BOOST_AUTO_TEST_CASE(block_coinbase_height_accepts_expected_prefix)
{
    CBlock block;
    block.vtx = {MakeCoinbaseWithScriptSig(CScript{} << 100 << OP_0)};

    BOOST_CHECK(Consensus::CheckBlockCoinbaseHeight(block, /*block_height=*/100));
}

BOOST_AUTO_TEST_CASE(block_coinbase_height_rejects_mismatch)
{
    CBlock block;
    block.vtx = {MakeCoinbaseWithScriptSig(CScript{} << 99)};

    const auto result{Consensus::CheckBlockCoinbaseHeight(block, /*block_height=*/100)};
    CheckRejectReason(result, Consensus::BlockConsensusIssue::Consensus, "bad-cb-height");
}

BOOST_AUTO_TEST_CASE(block_contextual_transaction_rules_apply_selected_context)
{
    CBlock block;
    block.vtx = {MakeCoinbaseWithScriptSig(CScript{} << 99)};

    Consensus::BlockContextualTransactionOptions options;
    options.block_height = 100;
    options.locktime_cutoff = 0;
    options.enforce_coinbase_height = false;

    BOOST_CHECK(Consensus::CheckBlockContextualTransactionRules(block, options));

    options.enforce_coinbase_height = true;
    const auto height_check{Consensus::CheckBlockContextualTransactionRules(block, options)};
    CheckRejectReason(height_check, Consensus::BlockConsensusIssue::Consensus, "bad-cb-height");
}

BOOST_AUTO_TEST_CASE(block_contextual_transaction_options_use_header_context)
{
    CBlock block;
    block.nTime = 99;

    const Consensus::BlockHeaderContext headers{100, 88, 0};

    const auto block_time_options{Consensus::BuildBlockContextualTransactionOptions(block, headers)};
    BOOST_CHECK_EQUAL(block_time_options.block_height, 100);
    BOOST_CHECK_EQUAL(block_time_options.locktime_cutoff, 99);
    BOOST_CHECK(!block_time_options.enforce_coinbase_height);

    const Consensus::BlockHeaderContext active_headers{
        100,
        88,
        0,
        Consensus::BlockDeploymentContext{
            .height_in_coinbase_active = true,
            .csv_active = true,
        },
    };
    const auto median_time_options{Consensus::BuildBlockContextualTransactionOptions(block, active_headers)};
    BOOST_CHECK_EQUAL(median_time_options.locktime_cutoff, 88);
    BOOST_CHECK(median_time_options.enforce_coinbase_height);
}

BOOST_AUTO_TEST_CASE(block_witness_expectation_uses_header_context)
{
    Consensus::BlockHeaderContext headers;
    BOOST_CHECK(!Consensus::ExpectWitnessCommitment(headers));

    const Consensus::BlockHeaderContext active_headers{
        0,
        0,
        0,
        Consensus::BlockDeploymentContext{.segwit_active = true},
    };
    BOOST_CHECK(Consensus::ExpectWitnessCommitment(active_headers));
}

BOOST_AUTO_TEST_SUITE_END()
