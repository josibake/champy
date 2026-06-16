// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_CORE_ADAPTER_TRANSACTION_H
#define BITCOIN_BITCOIN_CORE_ADAPTER_TRANSACTION_H

#include <bitcoin/protocol/script.h>
#include <bitcoin/protocol/transaction.h>

#include <vector>

class COutPoint;
class CScript;
struct CScriptWitness;
class CTransaction;
class CTxIn;
class CTxOut;

namespace bitcoin::core_adapter {

[[nodiscard]] script to_script(const CScript& value);
[[nodiscard]] witness_item to_witness_item(const std::vector<unsigned char>& value);
[[nodiscard]] std::vector<witness_item> to_witness(const CScriptWitness& value);
[[nodiscard]] outpoint to_outpoint(const COutPoint& value) noexcept;
[[nodiscard]] tx_input to_tx_input(const CTxIn& value);
[[nodiscard]] tx_output to_tx_output(const CTxOut& value);
[[nodiscard]] transaction to_transaction(const CTransaction& value);

[[nodiscard]] CScript to_core_script(script_ref value);
[[nodiscard]] std::vector<unsigned char> to_core_witness_item(const witness_item& value);
[[nodiscard]] COutPoint to_core_outpoint(outpoint value) noexcept;
[[nodiscard]] CTxIn to_core_tx_input(const tx_input& value);
[[nodiscard]] CTxOut to_core_tx_output(const tx_output& value);
[[nodiscard]] CTransaction to_core_transaction(const transaction& value);

} // namespace bitcoin::core_adapter

#endif // BITCOIN_BITCOIN_CORE_ADAPTER_TRANSACTION_H
