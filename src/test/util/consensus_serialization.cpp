// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/consensus_serialization.h>

#include <consensus/serialization.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <util/strencodings.h>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace test::consensus {
namespace {

std::vector<std::byte> ParseFixtureHex(std::string_view hex, std::string_view type)
{
    auto bytes{TryParseHex<std::byte>(hex)};
    if (!bytes) {
        throw std::runtime_error{std::string{type} + " fixture is not valid hex"};
    }
    return std::move(*bytes);
}

void AppendBytes(void* user_data, std::span<const std::byte> bytes)
{
    auto& out{*static_cast<std::vector<std::byte>*>(user_data)};
    out.insert(out.end(), bytes.begin(), bytes.end());
}

template <typename WriteFn>
std::string SerializeFixtureHex(WriteFn write)
{
    std::vector<std::byte> bytes;
    write(Consensus::ByteSinkRef{&bytes, AppendBytes});
    return HexStr(bytes);
}

template <typename T, typename ParseFn>
T ParseExactSerializedHex(std::string_view hex, std::string_view type, ParseFn parse)
{
    auto bytes{ParseFixtureHex(hex, type)};
    auto value{parse(bytes)};
    if (!value) {
        throw std::runtime_error{std::string{type} + " fixture is not exactly one complete serialized value"};
    }
    return std::move(*value);
}

} // namespace

CBlock ParseExactBlockHex(std::string_view hex)
{
    return ParseExactSerializedHex<CBlock>(hex, "block", [](std::span<const std::byte> bytes) {
        return Consensus::ParseBlock(bytes);
    });
}

CBlockHeader ParseExactBlockHeaderHex(std::string_view hex)
{
    return ParseExactSerializedHex<CBlockHeader>(hex, "block header", [](std::span<const std::byte> bytes) {
        return Consensus::ParseBlockHeader(bytes);
    });
}

CTransaction ParseExactTransactionHex(std::string_view hex)
{
    return ParseExactSerializedHex<CTransaction>(hex, "transaction", [](std::span<const std::byte> bytes) {
        return Consensus::ParseTransaction(bytes);
    });
}

CTxOut ParseExactTxOutHex(std::string_view hex)
{
    return ParseExactSerializedHex<CTxOut>(hex, "txout", [](std::span<const std::byte> bytes) {
        return Consensus::ParseTxOut(bytes);
    });
}

std::string SerializeBlockHex(const CBlock& block)
{
    return SerializeFixtureHex([&](Consensus::ByteSinkRef out) { Consensus::SerializeBlock(block, out); });
}

std::string SerializeBlockHeaderHex(const CBlockHeader& header)
{
    return SerializeFixtureHex([&](Consensus::ByteSinkRef out) { Consensus::SerializeBlockHeader(header, out); });
}

std::string SerializeTransactionHex(const CTransaction& tx)
{
    return SerializeFixtureHex([&](Consensus::ByteSinkRef out) { Consensus::SerializeTransaction(tx, out); });
}

std::string SerializeTxOutHex(const CTxOut& txout)
{
    return SerializeFixtureHex([&](Consensus::ByteSinkRef out) { Consensus::SerializeTxOut(txout, out); });
}

} // namespace test::consensus
