// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/consensus_serialization.h>

#include <consensus/serialization.h>
#include <consensus/amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <cstddef>
#include <span>
#include <vector>

namespace {

CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(50 * COIN, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeWitnessTransaction()
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.resize(1);
    tx.vin[0].scriptWitness.stack.push_back({0x01, 0x02, 0x03});
    tx.vout.emplace_back(1 * COIN, CScript{} << OP_TRUE);
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

CBlock MakeWitnessBlock()
{
    CBlock block{MakeBlock()};
    block.vtx.push_back(MakeWitnessTransaction());
    return block;
}

void AppendBytes(void* user_data, std::span<const std::byte> bytes)
{
    auto& out{*static_cast<std::vector<std::byte>*>(user_data)};
    out.insert(out.end(), bytes.begin(), bytes.end());
}

template <typename WriteFn>
std::vector<std::byte> SerializeToBytes(WriteFn write)
{
    std::vector<std::byte> bytes;
    write(Consensus::ByteSinkRef{&bytes, AppendBytes});
    return bytes;
}

std::vector<std::byte> SerializeBlockBytes(const CBlock& block)
{
    return SerializeToBytes([&](Consensus::ByteSinkRef out) { Consensus::SerializeBlock(block, out); });
}

std::vector<std::byte> SerializeBlockHeaderBytes(const CBlockHeader& header)
{
    return SerializeToBytes([&](Consensus::ByteSinkRef out) { Consensus::SerializeBlockHeader(header, out); });
}

std::vector<std::byte> SerializeTransactionBytes(const CTransaction& tx)
{
    return SerializeToBytes([&](Consensus::ByteSinkRef out) { Consensus::SerializeTransaction(tx, out); });
}

std::vector<std::byte> SerializeTxOutBytes(const CTxOut& txout)
{
    return SerializeToBytes([&](Consensus::ByteSinkRef out) { Consensus::SerializeTxOut(txout, out); });
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

template <typename ParseFn>
void CheckRejectsTruncatedAndTrailingBytes(const std::vector<std::byte>& bytes, ParseFn parse)
{
    BOOST_REQUIRE(!bytes.empty());

    auto truncated{bytes};
    truncated.pop_back();
    BOOST_CHECK(!parse(truncated));

    auto trailing{bytes};
    trailing.push_back(std::byte{0});
    BOOST_CHECK(!parse(trailing));
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_serialization_tests)

BOOST_AUTO_TEST_CASE(production_codecs_reject_malformed_truncated_and_trailing_bytes)
{
    const CBlock block{MakeBlock()};

    CheckRejectsTruncatedAndTrailingBytes(SerializeBlockBytes(block), [](std::span<const std::byte> bytes) {
        return Consensus::ParseBlock(bytes);
    });
    CheckRejectsTruncatedAndTrailingBytes(SerializeBlockHeaderBytes(block), [](std::span<const std::byte> bytes) {
        return Consensus::ParseBlockHeader(bytes);
    });
    CheckRejectsTruncatedAndTrailingBytes(SerializeTransactionBytes(*block.vtx[0]), [](std::span<const std::byte> bytes) {
        return Consensus::ParseTransaction(bytes);
    });
    CheckRejectsTruncatedAndTrailingBytes(SerializeTxOutBytes(block.vtx[0]->vout[0]), [](std::span<const std::byte> bytes) {
        return Consensus::ParseTxOut(bytes);
    });

    const std::vector<std::byte> unknown_tx_optional_data{
        std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0},
    };
    BOOST_CHECK(!Consensus::ParseTransaction(unknown_tx_optional_data));

    std::vector<std::byte> noncanonical_txout(11);
    noncanonical_txout[8] = std::byte{253};
    BOOST_CHECK(!Consensus::ParseTxOut(noncanonical_txout));
}

BOOST_AUTO_TEST_CASE(production_header_parse_requires_exact_header_length)
{
    const std::vector<std::byte> header(80);

    BOOST_CHECK(!Consensus::ParseBlockHeader(std::span{header}.first(79)));
    BOOST_CHECK(Consensus::ParseBlockHeader(header));

    std::vector<std::byte> trailing_header{header};
    trailing_header.push_back(std::byte{0});
    BOOST_CHECK(!Consensus::ParseBlockHeader(trailing_header));
}

BOOST_AUTO_TEST_CASE(production_serialization_round_trips_consensus_types)
{
    const CBlock block{MakeBlock()};
    const CBlock witness_block{MakeWitnessBlock()};

    const auto block_bytes{SerializeBlockBytes(block)};
    const auto parsed_block{Consensus::ParseBlock(block_bytes)};
    BOOST_REQUIRE(parsed_block);
    BOOST_CHECK(parsed_block->GetHash() == block.GetHash());
    BOOST_CHECK_EQUAL(parsed_block->vtx.size(), block.vtx.size());
    const auto reserialized_block{SerializeBlockBytes(*parsed_block)};
    BOOST_CHECK(block_bytes == reserialized_block);

    const auto header_bytes{SerializeBlockHeaderBytes(block)};
    const auto parsed_header{Consensus::ParseBlockHeader(header_bytes)};
    BOOST_REQUIRE(parsed_header);
    BOOST_CHECK(parsed_header->GetHash() == block.GetHash());
    const auto reserialized_header{SerializeBlockHeaderBytes(*parsed_header)};
    BOOST_CHECK(header_bytes == reserialized_header);

    const auto tx_bytes{SerializeTransactionBytes(*block.vtx[0])};
    const auto parsed_tx{Consensus::ParseTransaction(tx_bytes)};
    BOOST_REQUIRE(parsed_tx);
    BOOST_CHECK(parsed_tx->GetHash() == block.vtx[0]->GetHash());
    const auto reserialized_tx{SerializeTransactionBytes(*parsed_tx)};
    BOOST_CHECK(tx_bytes == reserialized_tx);

    const auto txout_bytes{SerializeTxOutBytes(block.vtx[0]->vout[0])};
    const auto parsed_txout{Consensus::ParseTxOut(txout_bytes)};
    BOOST_REQUIRE(parsed_txout);
    BOOST_CHECK_EQUAL(parsed_txout->nValue, block.vtx[0]->vout[0].nValue);
    BOOST_CHECK(parsed_txout->scriptPubKey == block.vtx[0]->vout[0].scriptPubKey);
    const auto reserialized_txout{SerializeTxOutBytes(*parsed_txout)};
    BOOST_CHECK(txout_bytes == reserialized_txout);

    const auto witness_block_bytes{SerializeBlockBytes(witness_block)};
    const auto parsed_witness_block{Consensus::ParseBlock(witness_block_bytes)};
    BOOST_REQUIRE(parsed_witness_block);
    BOOST_CHECK(witness_block_bytes == SerializeBlockBytes(*parsed_witness_block));
    BOOST_CHECK_EQUAL(parsed_witness_block->vtx[1]->vin[0].scriptWitness.stack.size(), 1);
    BOOST_CHECK_GT(Consensus::SerializedSize(witness_block), Consensus::StrippedSerializedSize(witness_block));

    const CTransaction& witness_tx{*witness_block.vtx[1]};
    const auto witness_tx_bytes{SerializeTransactionBytes(witness_tx)};
    const auto parsed_witness_tx{Consensus::ParseTransaction(witness_tx_bytes)};
    BOOST_REQUIRE(parsed_witness_tx);
    BOOST_CHECK(witness_tx_bytes == SerializeTransactionBytes(*parsed_witness_tx));
    BOOST_CHECK_EQUAL(parsed_witness_tx->vin[0].scriptWitness.stack.size(), 1);
    BOOST_CHECK_GT(Consensus::SerializedSize(witness_tx), Consensus::StrippedSerializedSize(witness_tx));
}

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

BOOST_AUTO_TEST_CASE(production_serialized_sizes_match_existing_encodings)
{
    const CBlock block{MakeBlock()};
    const CTransaction& tx{*block.vtx[0]};
    const CTxIn& txin{tx.vin[0]};
    const CTxOut& txout{tx.vout[0]};
    const CTransactionRef witness_tx_ref{MakeWitnessTransaction()};
    const CTransaction& witness_tx{*witness_tx_ref};
    const CTxIn& witness_txin{witness_tx.vin[0]};

    BOOST_CHECK_EQUAL(Consensus::SerializedSize(block), GetSerializeSize(TX_WITH_WITNESS(block)));
    BOOST_CHECK_EQUAL(Consensus::StrippedSerializedSize(block), GetSerializeSize(TX_NO_WITNESS(block)));
    BOOST_CHECK_EQUAL(Consensus::SerializedSize(static_cast<const CBlockHeader&>(block)), GetSerializeSize(static_cast<const CBlockHeader&>(block)));
    BOOST_CHECK_EQUAL(Consensus::SerializedSize(tx), GetSerializeSize(TX_WITH_WITNESS(tx)));
    BOOST_CHECK_EQUAL(Consensus::StrippedSerializedSize(tx), GetSerializeSize(TX_NO_WITNESS(tx)));
    BOOST_CHECK_EQUAL(Consensus::SerializedInputBaseSize(txin), GetSerializeSize(TX_NO_WITNESS(txin)));
    BOOST_CHECK_EQUAL(Consensus::SerializedInputWitnessStackSize(txin), GetSerializeSize(txin.scriptWitness.stack));
    BOOST_CHECK_EQUAL(Consensus::SerializedSize(txout), GetSerializeSize(txout));
    BOOST_CHECK_GT(Consensus::SerializedSize(witness_tx), Consensus::StrippedSerializedSize(witness_tx));
    BOOST_CHECK_EQUAL(Consensus::SerializedInputBaseSize(witness_txin), GetSerializeSize(TX_NO_WITNESS(witness_txin)));
    BOOST_CHECK_EQUAL(Consensus::SerializedInputWitnessStackSize(witness_txin), GetSerializeSize(witness_txin.scriptWitness.stack));
    BOOST_CHECK_GT(Consensus::SerializedInputWitnessStackSize(witness_txin), 0);
}

BOOST_AUTO_TEST_SUITE_END()
