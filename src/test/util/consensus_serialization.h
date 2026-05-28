// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_CONSENSUS_SERIALIZATION_H
#define BITCOIN_TEST_UTIL_CONSENSUS_SERIALIZATION_H

#include <string>
#include <string_view>

class CBlock;
class CBlockHeader;
class CTransaction;
class CTxOut;

namespace test::consensus {

[[nodiscard]] CBlock ParseExactBlockHex(std::string_view hex);
[[nodiscard]] CBlockHeader ParseExactBlockHeaderHex(std::string_view hex);
[[nodiscard]] CTransaction ParseExactTransactionHex(std::string_view hex);
[[nodiscard]] CTxOut ParseExactTxOutHex(std::string_view hex);

[[nodiscard]] std::string SerializeBlockHex(const CBlock& block);
[[nodiscard]] std::string SerializeBlockHeaderHex(const CBlockHeader& header);
[[nodiscard]] std::string SerializeTransactionHex(const CTransaction& tx);
[[nodiscard]] std::string SerializeTxOutHex(const CTxOut& txout);

} // namespace test::consensus

#endif // BITCOIN_TEST_UTIL_CONSENSUS_SERIALIZATION_H
