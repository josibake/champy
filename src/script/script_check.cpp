// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/script_check.h>

#include <script/interpreter.h>
#include <tinyformat.h>

std::optional<std::pair<ScriptError, std::string>> CScriptCheck::operator()()
{
    const CScript& scriptSig = m_tx_to->vin[m_input_index].scriptSig;
    const CScriptWitness* witness = &m_tx_to->vin[m_input_index].scriptWitness;
    ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
    if (VerifyScript(scriptSig, m_tx_out.scriptPubKey, witness, m_flags, CachingTransactionSignatureChecker(m_tx_to, m_input_index, m_tx_out.nValue, m_cache_store, *m_signature_cache, *m_txdata), &error)) {
        return std::nullopt;
    } else {
        auto debug_str = strprintf("input %i of %s (wtxid %s), spending %s:%i", m_input_index, m_tx_to->GetHash().ToString(), m_tx_to->GetWitnessHash().ToString(), m_tx_to->vin[m_input_index].prevout.hash.ToString(), m_tx_to->vin[m_input_index].prevout.n);
        return std::make_pair(error, std::move(debug_str));
    }
}
