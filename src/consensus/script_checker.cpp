// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/script_checker.h>

#include <consensus/predicates.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script_error.h>

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace Consensus {

BlockSpendResult<void> DirectBlockScriptChecker::Check(const TransactionScriptCheckPlan& check)
{
    const CTransactionRef& tx_ref{check.tx};
    const CTransaction& tx{*tx_ref};
    if (IsCoinbase(tx)) return {};

    assert(check.spent_outputs.size() == tx.vin.size());
    PrecomputedTransactionData txdata;
    txdata.Init(tx, std::vector<CTxOut>{check.spent_outputs});

    for (unsigned int input_index{0}; input_index < tx.vin.size(); ++input_index) {
        ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
        const CTxOut& prevout{txdata.m_spent_outputs[input_index]};
        if (!VerifyScript(tx.vin[input_index].scriptSig,
                          prevout.scriptPubKey,
                          &tx.vin[input_index].scriptWitness,
                          check.flags,
                          TransactionSignatureChecker(&tx, input_index, prevout.nValue, txdata, MissingDataBehavior::ASSERT_FAIL),
                          &error)) {
            return Consensus::Unexpected<BlockSpendError>{BlockSpendError{
                .issue = BlockConsensusIssue::Consensus,
                .reject_reason = "block-script-verify-flag-failed (" + std::string{ScriptErrorString(error)} + ")",
                .debug_message = "input " + std::to_string(input_index) + " of " + tx.GetHash().ToString() + " (wtxid " + tx.GetWitnessHash().ToString() + "), spending " + tx.vin[input_index].prevout.hash.ToString() + ":" + std::to_string(tx.vin[input_index].prevout.n),
            }};
        }
    }

    return {};
}

BlockSpendResult<void> DirectBlockScriptChecker::Complete()
{
    return {};
}

} // namespace Consensus
