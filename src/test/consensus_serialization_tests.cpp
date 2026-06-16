// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/consensus_serialization.h>

#include <consensus/amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <stdexcept>

namespace {

CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(50 * COIN, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CBlock MakeBlock()
{
    CBlock block;
    block.nVersion = 4;
    block.hashPrevBlock = uint256::ONE;
    block.nTime = 1;
    block.nBits = 2;
    block.nNonce = 3;
    block.vtx = {MakeCoinbase()};
    return block;
}

template <typename Fn>
void CheckThrowsRuntimeError(Fn fn)
{
    try {
        (void)fn();
        BOOST_ERROR("expected std::runtime_error");
    } catch (const std::runtime_error&) {
    }
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_serialization_tests)

BOOST_AUTO_TEST_CASE(exact_fixture_parsing_rejects_invalid_hex)
{
    CheckThrowsRuntimeError([] { return test::consensus::ParseExactBlockHeaderHex("zz"); });
    CheckThrowsRuntimeError([] { return test::consensus::ParseExactBlockHeaderHex("00"); });
}

BOOST_AUTO_TEST_CASE(exact_fixture_parsing_rejects_trailing_bytes)
{
    const CBlock block{MakeBlock()};

    CheckThrowsRuntimeError([&] { return test::consensus::ParseExactBlockHex(test::consensus::SerializeBlockHex(block) + "00"); });
    CheckThrowsRuntimeError([&] { return test::consensus::ParseExactBlockHeaderHex(test::consensus::SerializeBlockHeaderHex(block) + "00"); });
    CheckThrowsRuntimeError([&] { return test::consensus::ParseExactTransactionHex(test::consensus::SerializeTransactionHex(*block.vtx[0]) + "00"); });
    CheckThrowsRuntimeError([&] { return test::consensus::ParseExactTxOutHex(test::consensus::SerializeTxOutHex(block.vtx[0]->vout[0]) + "00"); });
}

BOOST_AUTO_TEST_CASE(exact_fixture_serialization_round_trips_consensus_types)
{
    const CBlock block{MakeBlock()};

    const CBlock parsed_block{test::consensus::ParseExactBlockHex(test::consensus::SerializeBlockHex(block))};
    BOOST_CHECK(parsed_block.GetHash() == block.GetHash());
    BOOST_CHECK_EQUAL(parsed_block.vtx.size(), block.vtx.size());

    const CBlockHeader parsed_header{test::consensus::ParseExactBlockHeaderHex(test::consensus::SerializeBlockHeaderHex(block))};
    BOOST_CHECK(parsed_header.GetHash() == block.GetHash());

    const CTransaction parsed_tx{test::consensus::ParseExactTransactionHex(test::consensus::SerializeTransactionHex(*block.vtx[0]))};
    BOOST_CHECK(parsed_tx.GetHash() == block.vtx[0]->GetHash());

    const CTxOut parsed_txout{test::consensus::ParseExactTxOutHex(test::consensus::SerializeTxOutHex(block.vtx[0]->vout[0]))};
    BOOST_CHECK_EQUAL(parsed_txout.nValue, block.vtx[0]->vout[0].nValue);
    BOOST_CHECK(parsed_txout.scriptPubKey == block.vtx[0]->vout[0].scriptPubKey);
}

BOOST_AUTO_TEST_SUITE_END()
