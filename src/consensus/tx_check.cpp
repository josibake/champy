// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <consensus/amount.h>
#include <consensus/predicates.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>

#include <set>
#include <string>

namespace Consensus {

namespace {

TransactionCheckError InvalidTransactionCheck(const std::string& reject_reason, const std::string& debug_message = {})
{
    return TransactionCheckError{
        .reject_reason = reject_reason,
        .debug_message = debug_message,
    };
}

TransactionCheckResult<void> InvalidConsensusTransaction(const std::string& reject_reason)
{
    return Consensus::Unexpected<TransactionCheckError>{
        InvalidTransactionCheck(reject_reason)};
}

} // namespace

TransactionCheckResult<void> CheckTransaction(const CTransaction& tx)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return InvalidConsensusTransaction("bad-txns-vin-empty");
    if (tx.vout.empty())
        return InvalidConsensusTransaction("bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(TX_NO_WITNESS(tx)) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT) {
        return InvalidConsensusTransaction("bad-txns-oversize");
    }

    // Check for negative or overflow output values (see CVE-2010-5139)
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return InvalidConsensusTransaction("bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return InvalidConsensusTransaction("bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return InvalidConsensusTransaction("bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs (see CVE-2018-17144)
    // UTXO input checks can prove all inputs are available, and coin-effect
    // commit code can mark inputs spent, but neither implies inputs are unique.
    // Failure to run this check will result in either a crash or an inflation bug, depending on the implementation of
    // the underlying coins database.
    std::set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin) {
        if (!vInOutPoints.insert(txin.prevout).second)
            return InvalidConsensusTransaction("bad-txns-inputs-duplicate");
    }

    if (Consensus::IsCoinbase(tx))
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return InvalidConsensusTransaction("bad-cb-length");
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (Consensus::IsCoinbase(txin.prevout))
                return InvalidConsensusTransaction("bad-txns-prevout-null");
    }

    return {};
}

} // namespace Consensus
