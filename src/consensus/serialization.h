// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SERIALIZATION_H
#define BITCOIN_CONSENSUS_SERIALIZATION_H

#include <primitives/block.h>
#include <primitives/transaction.h>

#include <cstddef>
#include <optional>
#include <span>

namespace Consensus {

/**
 * Non-owning byte sink projection.
 *
 * The referenced sink must outlive the serialization call. The write function
 * may throw if the sink cannot accept bytes.
 */
class ByteSinkRef
{
public:
    using WriteFn = void (*)(void* user_data, std::span<const std::byte> bytes);

    constexpr ByteSinkRef(void* user_data, WriteFn write) noexcept : m_user_data{user_data}, m_write{write} {}

    void Write(std::span<const std::byte> bytes) const { m_write(m_user_data, bytes); }

private:
    void* m_user_data;
    WriteFn m_write;
};

[[nodiscard]] std::optional<CBlock> ParseBlock(std::span<const std::byte> bytes);
[[nodiscard]] std::optional<CBlockHeader> ParseBlockHeader(std::span<const std::byte> bytes);
[[nodiscard]] std::optional<CTransaction> ParseTransaction(std::span<const std::byte> bytes);
[[nodiscard]] std::optional<CTxOut> ParseTxOut(std::span<const std::byte> bytes);

void SerializeBlock(const CBlock& block, ByteSinkRef out);
void SerializeBlockHeader(const CBlockHeader& header, ByteSinkRef out);
void SerializeTransaction(const CTransaction& tx, ByteSinkRef out);
void SerializeTxOut(const CTxOut& txout, ByteSinkRef out);

[[nodiscard]] std::size_t SerializedSize(const CBlock& block);
[[nodiscard]] std::size_t SerializedSize(const CBlockHeader& header);
[[nodiscard]] std::size_t SerializedSize(const CTransaction& tx);
[[nodiscard]] std::size_t SerializedSize(const CTxOut& txout);

[[nodiscard]] std::size_t StrippedSerializedSize(const CBlock& block);
[[nodiscard]] std::size_t StrippedSerializedSize(const CTransaction& tx);
[[nodiscard]] std::size_t SerializedInputBaseSize(const CTxIn& txin);
[[nodiscard]] std::size_t SerializedInputWitnessStackSize(const CTxIn& txin);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SERIALIZATION_H
