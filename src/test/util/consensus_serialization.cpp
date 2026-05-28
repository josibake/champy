// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/consensus_serialization.h>

#include <crypto/hex_base.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <util/strencodings.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace test::consensus {
namespace {

std::vector<uint8_t> ParseFixtureHex(std::string_view hex, std::string_view type)
{
    auto bytes{TryParseHex<uint8_t>(hex)};
    if (!bytes) {
        throw std::runtime_error{std::string{type} + " fixture is not valid hex"};
    }
    return std::move(*bytes);
}

void CheckFullyConsumed(const SpanReader& reader, std::string_view type)
{
    if (!reader.empty()) {
        throw std::runtime_error{std::string{type} + " fixture has trailing bytes"};
    }
}

template <typename WriteFn>
std::string SerializeFixtureHex(WriteFn write)
{
    std::vector<uint8_t> bytes;
    VectorWriter writer{bytes, 0};
    write(writer);
    return HexStr(bytes);
}

template <typename T, typename ReadFn>
T ParseExactSerializedHex(std::string_view hex, std::string_view type, ReadFn read)
{
    auto bytes{ParseFixtureHex(hex, type)};
    SpanReader reader{bytes};
    T value;
    try {
        read(reader, value);
    } catch (const std::exception& e) {
        throw std::runtime_error{std::string{type} + " fixture is not a complete serialized value: " + e.what()};
    }
    CheckFullyConsumed(reader, type);
    return value;
}

} // namespace

CBlock ParseExactBlockHex(std::string_view hex)
{
    return ParseExactSerializedHex<CBlock>(hex, "block", [](SpanReader& reader, CBlock& block) {
        reader >> TX_WITH_WITNESS(block);
    });
}

CBlockHeader ParseExactBlockHeaderHex(std::string_view hex)
{
    return ParseExactSerializedHex<CBlockHeader>(hex, "block header", [](SpanReader& reader, CBlockHeader& header) {
        reader >> header;
    });
}

CTransaction ParseExactTransactionHex(std::string_view hex)
{
    CMutableTransaction tx{ParseExactSerializedHex<CMutableTransaction>(hex, "transaction", [](SpanReader& reader, CMutableTransaction& tx) {
        reader >> TX_WITH_WITNESS(tx);
    })};
    return CTransaction{tx};
}

CTxOut ParseExactTxOutHex(std::string_view hex)
{
    return ParseExactSerializedHex<CTxOut>(hex, "txout", [](SpanReader& reader, CTxOut& txout) {
        reader >> txout;
    });
}

std::string SerializeBlockHex(const CBlock& block)
{
    return SerializeFixtureHex([&](VectorWriter& writer) { writer << TX_WITH_WITNESS(block); });
}

std::string SerializeBlockHeaderHex(const CBlockHeader& header)
{
    return SerializeFixtureHex([&](VectorWriter& writer) { writer << header; });
}

std::string SerializeTransactionHex(const CTransaction& tx)
{
    return SerializeFixtureHex([&](VectorWriter& writer) { writer << TX_WITH_WITNESS(tx); });
}

std::string SerializeTxOutHex(const CTxOut& txout)
{
    return SerializeFixtureHex([&](VectorWriter& writer) { writer << txout; });
}

} // namespace test::consensus
