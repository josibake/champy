// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/predicates.h>

#include <primitives/transaction.h>
#include <script/script.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace Consensus {

bool IsCoinbase(const COutPoint& outpoint)
{
    return outpoint.IsNull();
}

bool IsCoinbase(const CTransaction& tx)
{
    return tx.IsCoinBase();
}

bool HasWitness(const CTransaction& tx)
{
    return tx.HasWitness();
}

bool SignalsRBF(const CTxIn& txin)
{
    return txin.nSequence < CTxIn::MAX_SEQUENCE_NONFINAL;
}

bool SignalsRBF(const CTransaction& tx)
{
    return std::ranges::any_of(tx.vin, [](const CTxIn& txin) {
        return SignalsRBF(txin);
    });
}

bool HasRelativeLocktime(const CTxIn& txin)
{
    return (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) == 0 &&
           txin.nSequence != CTxIn::SEQUENCE_FINAL;
}

bool RelativeLocktimeIsTime(const CTxIn& txin)
{
    return txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
}

bool LocktimeIsHeight(uint32_t locktime)
{
    return locktime < LOCKTIME_THRESHOLD;
}

bool LocktimeIsTime(uint32_t locktime)
{
    return !LocktimeIsHeight(locktime);
}

bool LocktimeIsHeight(const CTransaction& tx)
{
    return LocktimeIsHeight(tx.nLockTime);
}

bool LocktimeIsTime(const CTransaction& tx)
{
    return LocktimeIsTime(tx.nLockTime);
}

bool IsUnspendable(const CScript& script)
{
    return IsUnspendable(ScriptView{script});
}

bool IsUnspendable(const CTxOut& txout)
{
    return IsUnspendable(txout.scriptPubKey);
}

bool IsPayToScriptHash(const CScript& script)
{
    return IsPayToScriptHash(ScriptView{script});
}

std::optional<WitnessProgramView> GetWitnessProgramView(ScriptView script)
{
    return GetWitnessProgram(script);
}

std::optional<WitnessProgram> GetWitnessProgram(const CScript& script)
{
    const auto view{GetWitnessProgramView(ScriptView{script})};
    if (!view) return std::nullopt;

    return WitnessProgram{
        .version = view->version,
        .program = std::vector<unsigned char>{view->program.begin(), view->program.end()},
    };
}

} // namespace Consensus
