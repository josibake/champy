// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/core_adapter/transaction.h>

#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>

namespace bitcoin::core_adapter {
namespace {

[[nodiscard]] std::array<std::byte, 32> bytes_from(const uint256& value) noexcept
{
    std::array<std::byte, 32> bytes{};
    std::ranges::transform(
        std::span<const unsigned char>{value.data(), uint256::size()},
        bytes.begin(),
        [](unsigned char byte) { return static_cast<std::byte>(byte); });
    return bytes;
}

[[nodiscard]] txid to_txid(const Txid& value) noexcept
{
    return txid{bytes_from(value.ToUint256())};
}

template <typename Hash>
[[nodiscard]] uint256 to_uint256_id(Hash value) noexcept
{
    std::array<unsigned char, 32> bytes{};
    std::ranges::transform(as_bytes(value), bytes.begin(), [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
    return uint256{std::span<const unsigned char>{bytes}};
}

[[nodiscard]] std::int32_t to_transaction_version(std::uint32_t value) noexcept
{
    return std::bit_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t to_core_transaction_version(std::int32_t value) noexcept
{
    return std::bit_cast<std::uint32_t>(value);
}

} // namespace

script to_script(const CScript& value)
{
    return script{std::as_bytes(std::span<const unsigned char>{value.data(), value.size()})};
}

witness_item to_witness_item(const std::vector<unsigned char>& value)
{
    return witness_item{std::as_bytes(std::span<const unsigned char>{value.data(), value.size()})};
}

std::vector<witness_item> to_witness(const CScriptWitness& value)
{
    std::vector<witness_item> result;
    result.reserve(value.stack.size());
    std::ranges::transform(value.stack, std::back_inserter(result), [](const auto& item) {
        return to_witness_item(item);
    });
    return result;
}

outpoint to_outpoint(const COutPoint& value) noexcept
{
    return outpoint{to_txid(value.hash), tx_output_index{value.n}};
}

tx_input to_tx_input(const CTxIn& value)
{
    return tx_input{to_outpoint(value.prevout), to_script(value.scriptSig), value.nSequence, to_witness(value.scriptWitness)};
}

tx_output to_tx_output(const CTxOut& value)
{
    return tx_output{amount{value.nValue}, to_script(value.scriptPubKey)};
}

transaction to_transaction(const CTransaction& value)
{
    std::vector<tx_input> inputs;
    inputs.reserve(value.vin.size());
    std::ranges::transform(value.vin, std::back_inserter(inputs), [](const CTxIn& input) {
        return to_tx_input(input);
    });

    std::vector<tx_output> outputs;
    outputs.reserve(value.vout.size());
    std::ranges::transform(value.vout, std::back_inserter(outputs), [](const CTxOut& output) {
        return to_tx_output(output);
    });

    return transaction{to_transaction_version(value.version), std::move(inputs), std::move(outputs), value.nLockTime};
}

CScript to_core_script(script_ref value)
{
    const auto bytes{as_bytes(value)};
    CScript result;
    result.reserve(bytes.size());
    std::ranges::transform(bytes, std::back_inserter(result), [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
    return result;
}

std::vector<unsigned char> to_core_witness_item(const witness_item& value)
{
    const auto bytes{as_bytes(value)};
    std::vector<unsigned char> result;
    result.reserve(bytes.size());
    std::ranges::transform(bytes, std::back_inserter(result), [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
    return result;
}

COutPoint to_core_outpoint(outpoint value) noexcept
{
    return COutPoint{Txid::FromUint256(to_uint256_id(value.txid())), value.index().value()};
}

CTxIn to_core_tx_input(const tx_input& value)
{
    CTxIn result{to_core_outpoint(value.previous_output()), to_core_script(value.script()), value.sequence()};
    result.scriptWitness.stack.reserve(value.witness().size());
    std::ranges::transform(value.witness(), std::back_inserter(result.scriptWitness.stack), [](const witness_item& item) {
        return to_core_witness_item(item);
    });
    return result;
}

CTxOut to_core_tx_output(const tx_output& value)
{
    return CTxOut{value.value().satoshis(), to_core_script(value.script())};
}

CTransaction to_core_transaction(const transaction& value)
{
    CMutableTransaction tx;
    tx.version = to_core_transaction_version(value.version());
    tx.nLockTime = value.locktime();
    tx.vin.reserve(value.inputs().size());
    std::ranges::transform(value.inputs(), std::back_inserter(tx.vin), [](const tx_input& input) {
        return to_core_tx_input(input);
    });
    tx.vout.reserve(value.outputs().size());
    std::ranges::transform(value.outputs(), std::back_inserter(tx.vout), [](const tx_output& output) {
        return to_core_tx_output(output);
    });
    return CTransaction{std::move(tx)};
}

} // namespace bitcoin::core_adapter
