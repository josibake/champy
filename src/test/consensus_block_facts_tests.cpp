// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_facts.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

namespace {

CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeRegularTx(uint32_t output_index = 0)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.n = output_index;
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CScript WitnessCommitmentScript()
{
    CScript script;
    script.resize(MINIMUM_WITNESS_COMMITMENT);
    script[0] = OP_RETURN;
    script[1] = 0x24;
    script[2] = 0xaa;
    script[3] = 0x21;
    script[4] = 0xa9;
    script[5] = 0xed;
    return script;
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_block_facts_tests)

BOOST_AUTO_TEST_CASE(block_facts_match_existing_helpers_for_empty_block)
{
    const CBlock block;

    const MerkleRootResult merkle{BlockMerkleRootWithMutation(block)};
    const auto facts{Consensus::ComputeBlockFacts(block)};

    BOOST_CHECK_EQUAL(facts.structure.transaction_count, 0);
    BOOST_CHECK(facts.structure.merkle_root == merkle.root);
    BOOST_CHECK_EQUAL(facts.structure.merkle_mutated, merkle.mutated);
    BOOST_CHECK(facts.witness_merkle_root == BlockWitnessMerkleRoot(block));
    BOOST_CHECK(!facts.has_witness);
    BOOST_CHECK(!facts.witness_commitment_index.has_value());
    BOOST_CHECK_EQUAL(facts.structure.stripped_size, ::GetSerializeSize(TX_NO_WITNESS(block)));
    BOOST_CHECK_EQUAL(facts.weight, GetBlockWeight(block));
}

BOOST_AUTO_TEST_CASE(block_facts_report_merkle_mutation)
{
    CBlock block;
    const auto tx{MakeRegularTx()};
    block.vtx = {tx, tx};

    const MerkleRootResult merkle{BlockMerkleRootWithMutation(block)};
    const auto facts{Consensus::ComputeBlockFacts(block)};

    BOOST_CHECK(merkle.mutated);
    BOOST_CHECK(facts.structure.merkle_mutated);
    BOOST_CHECK(facts.structure.merkle_root == merkle.root);
}

BOOST_AUTO_TEST_CASE(block_facts_capture_witness_and_commitment_shape)
{
    CMutableTransaction coinbase{*MakeCoinbase()};
    coinbase.vout.resize(4);
    coinbase.vout[0].scriptPubKey = WitnessCommitmentScript();
    coinbase.vout[1].scriptPubKey = CScript{} << OP_TRUE;
    coinbase.vout[2].scriptPubKey = WitnessCommitmentScript();
    coinbase.vout[3].scriptPubKey = CScript{} << OP_RETURN;

    CMutableTransaction witness_tx{*MakeRegularTx()};
    witness_tx.vin[0].scriptWitness.stack.emplace_back(32, 0x01);

    CBlock block;
    block.vtx = {MakeTransactionRef(coinbase), MakeTransactionRef(witness_tx)};

    const auto facts{Consensus::ComputeBlockFacts(block)};

    BOOST_CHECK(facts.has_witness);
    BOOST_REQUIRE(facts.witness_commitment_index.has_value());
    BOOST_CHECK_EQUAL(*facts.witness_commitment_index, GetWitnessCommitmentIndex(block));
    BOOST_CHECK_EQUAL(*facts.witness_commitment_index, 2);
    BOOST_CHECK(facts.witness_merkle_root == BlockWitnessMerkleRoot(block));
    BOOST_CHECK_EQUAL(facts.structure.stripped_size, ::GetSerializeSize(TX_NO_WITNESS(block)));
    BOOST_CHECK_EQUAL(facts.weight, GetBlockWeight(block));
}

BOOST_AUTO_TEST_CASE(block_facts_can_be_built_from_transaction_span_and_explicit_sizes)
{
    CMutableTransaction coinbase{*MakeCoinbase()};
    coinbase.vout[0].scriptPubKey = WitnessCommitmentScript();

    CMutableTransaction witness_tx{*MakeRegularTx()};
    witness_tx.vin[0].scriptWitness.stack.emplace_back(32, 0x01);

    CBlock block;
    block.vtx = {MakeTransactionRef(coinbase), MakeTransactionRef(witness_tx)};

    const auto from_block{Consensus::ComputeBlockFacts(block)};
    const auto from_span{Consensus::ComputeBlockFacts(
        block.vtx,
        ::GetSerializeSize(TX_NO_WITNESS(block)),
        Consensus::GetBlockWeight(block))};

    BOOST_CHECK_EQUAL(from_span.structure.transaction_count, from_block.structure.transaction_count);
    BOOST_CHECK(from_span.structure.merkle_root == from_block.structure.merkle_root);
    BOOST_CHECK_EQUAL(from_span.structure.merkle_mutated, from_block.structure.merkle_mutated);
    BOOST_CHECK_EQUAL(from_span.structure.stripped_size, from_block.structure.stripped_size);
    BOOST_CHECK(from_span.witness_merkle_root == from_block.witness_merkle_root);
    BOOST_CHECK_EQUAL(from_span.has_witness, from_block.has_witness);
    BOOST_CHECK(from_span.witness_commitment_index == from_block.witness_commitment_index);
    BOOST_CHECK_EQUAL(from_span.weight, from_block.weight);
}

BOOST_AUTO_TEST_CASE(structural_facts_match_structure_only_helpers)
{
    CMutableTransaction coinbase{*MakeCoinbase()};
    coinbase.vout[0].scriptPubKey = WitnessCommitmentScript();

    CMutableTransaction witness_tx{*MakeRegularTx()};
    witness_tx.vin[0].scriptWitness.stack.emplace_back(32, 0x01);

    CBlock block;
    block.vtx = {MakeTransactionRef(coinbase), MakeTransactionRef(witness_tx)};

    const auto facts{Consensus::ComputeBlockStructuralFacts(block)};

    BOOST_CHECK_EQUAL(facts.transaction_count, 2);
    BOOST_CHECK(facts.merkle_root == BlockMerkleRoot(block));
    BOOST_CHECK_EQUAL(facts.merkle_mutated, false);
    BOOST_CHECK_EQUAL(facts.stripped_size, ::GetSerializeSize(TX_NO_WITNESS(block)));
}

BOOST_AUTO_TEST_SUITE_END()
