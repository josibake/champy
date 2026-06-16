// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PREDICATES_H
#define BITCOIN_CONSENSUS_PREDICATES_H

#include <consensus/script_view.h>

#include <cstdint>
#include <optional>
#include <vector>

class COutPoint;
class CScript;
class CTransaction;
class CTxIn;
class CTxOut;

namespace Consensus {

struct WitnessProgram {
    int version;
    std::vector<unsigned char> program;
};

[[nodiscard]] bool IsCoinbase(const COutPoint& outpoint);
[[nodiscard]] bool IsCoinbase(const CTransaction& tx);
[[nodiscard]] bool HasWitness(const CTransaction& tx);

[[nodiscard]] bool SignalsRBF(const CTxIn& txin);
[[nodiscard]] bool SignalsRBF(const CTransaction& tx);

/**
 * Return whether the input sequence field encodes a relative locktime.
 *
 * This is only the input-level BIP68 field condition. Consensus enforcement
 * also depends on transaction version, deployment state, and block context.
 */
[[nodiscard]] bool HasRelativeLocktime(const CTxIn& txin);
[[nodiscard]] bool RelativeLocktimeIsTime(const CTxIn& txin);

[[nodiscard]] bool LocktimeIsHeight(uint32_t locktime);
[[nodiscard]] bool LocktimeIsTime(uint32_t locktime);
[[nodiscard]] bool LocktimeIsHeight(const CTransaction& tx);
[[nodiscard]] bool LocktimeIsTime(const CTransaction& tx);

[[nodiscard]] bool IsUnspendable(const CScript& script);
[[nodiscard]] bool IsUnspendable(const CTxOut& txout);
[[nodiscard]] bool IsPayToScriptHash(const CScript& script);

[[nodiscard]] std::optional<WitnessProgramView> GetWitnessProgramView(ScriptView script);
[[nodiscard]] std::optional<WitnessProgram> GetWitnessProgram(const CScript& script);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PREDICATES_H
