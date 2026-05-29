// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_VALIDATION_H
#define BITCOIN_SCRIPT_VALIDATION_H

#include <kernel/cs_main.h>
#include <script/script_check.h>
#include <script/verify_flags.h>
#include <validation_state.h>

#include <vector>

class CCoinsViewCache;
class CTransaction;
class ValidationCache;

/**
 * Check whether all of this transaction's input scripts succeed.
 *
 * This involves ECDSA signature checks so can be computationally intensive.
 * Call this only after cheap input checks have passed.
 *
 * If pvChecks is not nullptr, script checks are pushed onto it instead of being
 * performed inline. Cache hits do not add checks.
 *
 * Non-static and redeclared in src/test/txvalidationcache_tests.cpp.
 */
bool CheckInputScripts(const CTransaction& tx, TxValidationState& state,
                       const CCoinsViewCache& inputs, script_verify_flags flags, bool cacheSigStore,
                       bool cacheFullScriptStore, PrecomputedTransactionData& txdata,
                       ValidationCache& validation_cache,
                       std::vector<CScriptCheck>* pvChecks = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_SCRIPT_VALIDATION_H
