// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SCRIPT_CHECK_H
#define BITCOIN_SCRIPT_SCRIPT_CHECK_H

#include <primitives/transaction.h>
#include <script/script_error.h>
#include <script/sigcache.h>
#include <script/verify_flags.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

struct PrecomputedTransactionData;

/**
 * Closure representing one script verification.
 *
 * Borrowing checks require the caller to keep the transaction and precomputed
 * data alive until operator() finishes. Queued block checks use the owning
 * constructor.
 */
class CScriptCheck
{
private:
    CTxOut m_tx_out;
    CTransactionRef m_tx_to_owner;
    std::shared_ptr<PrecomputedTransactionData> m_txdata_owner;
    const CTransaction* m_tx_to;
    unsigned int m_input_index;
    script_verify_flags m_flags;
    bool m_cache_store;
    PrecomputedTransactionData* m_txdata;
    SignatureCache* m_signature_cache;

public:
    CScriptCheck(const CTxOut& outIn, const CTransaction& txToIn, SignatureCache& signature_cache, unsigned int nInIn, script_verify_flags flags, bool cacheIn, PrecomputedTransactionData* txdataIn)
        : m_tx_out(outIn), m_tx_to(&txToIn), m_input_index(nInIn), m_flags(flags), m_cache_store(cacheIn), m_txdata(txdataIn), m_signature_cache(&signature_cache)
    {
    }
    CScriptCheck(const CTxOut& outIn, CTransactionRef txToIn, SignatureCache& signature_cache, unsigned int nInIn, script_verify_flags flags, bool cacheIn, std::shared_ptr<PrecomputedTransactionData> txdataIn)
        : m_tx_out(outIn), m_tx_to_owner(std::move(txToIn)), m_txdata_owner(std::move(txdataIn)), m_tx_to(m_tx_to_owner.get()), m_input_index(nInIn), m_flags(flags), m_cache_store(cacheIn), m_txdata(m_txdata_owner.get()), m_signature_cache(&signature_cache)
    {
    }

    CScriptCheck(const CScriptCheck&) = delete;
    CScriptCheck& operator=(const CScriptCheck&) = delete;
    CScriptCheck(CScriptCheck&&) = default;
    CScriptCheck& operator=(CScriptCheck&&) = default;

    std::optional<std::pair<ScriptError, std::string>> operator()();
};

// CScriptCheck is used a lot in std::vector, make sure that's efficient
static_assert(std::is_nothrow_move_assignable_v<CScriptCheck>);
static_assert(std::is_nothrow_move_constructible_v<CScriptCheck>);
static_assert(std::is_nothrow_destructible_v<CScriptCheck>);

#endif // BITCOIN_SCRIPT_SCRIPT_CHECK_H
