// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_script_check_adapters.h>

#include <chainstate.h>
#include <coins.h>
#include <consensus/expected.h>
#include <crypto/sha256.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script_check.h>
#include <script/script_error.h>
#include <validation/script_validation.h>
#include <span.h>
#include <tinyformat.h>
#include <validation_state.h>

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

CoreScriptValidationCache::CoreScriptValidationCache(ValidationCache& validation_cache)
    : m_validation_cache{validation_cache}
{
}

uint256 CoreScriptValidationCache::ExecutionCacheEntry(const CTransaction& tx, script_verify_flags flags) const
{
    uint256 entry;
    CSHA256 hasher = m_validation_cache.ScriptExecutionCacheHasher();
    hasher.Write(UCharCast(tx.GetWitnessHash().begin()), 32).Write((unsigned char*)&flags, sizeof(flags)).Finalize(entry.begin());
    return entry;
}

bool CoreScriptValidationCache::ContainsScriptExecution(const uint256& entry, bool erase)
{
    AssertLockHeld(cs_main);
    return m_validation_cache.m_script_execution_cache.contains(entry, erase);
}

void CoreScriptValidationCache::StoreScriptExecution(const uint256& entry)
{
    AssertLockHeld(cs_main);
    m_validation_cache.m_script_execution_cache.insert(entry);
}

SignatureCache& CoreScriptValidationCache::SignatureCacheStore()
{
    return m_validation_cache.m_signature_cache;
}

namespace {

template <typename PrepareSpentOutputs>
bool CheckInputScriptsWithPreparedOutputs(const CTransaction& tx, TxValidationState& state, script_verify_flags flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData& txdata, CoreScriptValidationCache& validation_cache, std::vector<CScriptCheck>* pvChecks, PrepareSpentOutputs&& prepare_spent_outputs, const CTransactionRef* tx_ref = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    if (tx.IsCoinBase()) return true;

    if (pvChecks) {
        pvChecks->reserve(tx.vin.size());
    }

    // First check if script executions have been cached with the same
    // flags. Note that this assumes that the inputs provided are
    // correct (ie that the transaction hash which is in tx's prevouts
    // properly commits to the scriptPubKey in the inputs view of that
    // transaction).
    const uint256 hashCacheEntry{validation_cache.ExecutionCacheEntry(tx, flags)};
    // Compatibility note: script-cache access still relies on Core's external
    // `cs_main` lock. Track this in doc/legacy-compatibility.md instead of
    // expanding the lock contract into consensus code.
    if (validation_cache.ContainsScriptExecution(hashCacheEntry, !cacheFullScriptStore)) {
        return true;
    }

    if (!txdata.m_spent_outputs_ready) {
        prepare_spent_outputs();
    }
    assert(txdata.m_spent_outputs_ready);
    assert(txdata.m_spent_outputs.size() == tx.vin.size());

    std::shared_ptr<PrecomputedTransactionData> queued_txdata;
    if (pvChecks && tx_ref) {
        queued_txdata = std::make_shared<PrecomputedTransactionData>(txdata);
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        // We very carefully only pass in things to CScriptCheck which
        // are clearly committed to by tx' witness hash. This provides
        // a sanity check that our caching is not introducing consensus
        // failures through additional data in, eg, the coins being
        // spent being checked as a part of CScriptCheck.

        // Verify signature
        if (pvChecks) {
            if (tx_ref) {
                pvChecks->emplace_back(txdata.m_spent_outputs[i], *tx_ref, validation_cache.SignatureCacheStore(), i, flags, cacheSigStore, queued_txdata);
            } else {
                pvChecks->emplace_back(txdata.m_spent_outputs[i], tx, validation_cache.SignatureCacheStore(), i, flags, cacheSigStore, &txdata);
            }
        } else {
            CScriptCheck check{txdata.m_spent_outputs[i], tx, validation_cache.SignatureCacheStore(), i, flags, cacheSigStore, &txdata};
            if (auto result = check(); result.has_value()) {
                // Tx failures never trigger disconnections/bans.
                // This is so that network splits aren't triggered
                // either due to non-consensus relay policies (such as
                // non-standard DER encodings or non-null dummy
                // arguments) or due to new consensus rules introduced in
                // soft forks.
                if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                    return state.Invalid(TxValidationResult::TX_NOT_STANDARD, strprintf("mempool-script-verify-flag-failed (%s)", ScriptErrorString(result->first)), result->second);
                } else {
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, strprintf("block-script-verify-flag-failed (%s)", ScriptErrorString(result->first)), result->second);
                }
            }
        }
    }

    if (cacheFullScriptStore && !pvChecks) {
        // We executed all of the provided scripts, and were told to
        // cache the result. Do so now.
        validation_cache.StoreScriptExecution(hashCacheEntry);
    }

    return true;
}

bool CheckInputScriptsFromPlan(const Consensus::TransactionScriptCheckPlan& check, TxValidationState& state, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData& txdata, CoreScriptValidationCache& validation_cache, std::vector<CScriptCheck>* pvChecks) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    const CTransactionRef& tx_ref{check.tx};
    const CTransaction& tx{*tx_ref};
    assert(check.spent_outputs.size() == tx.vin.size());

    const auto prepare_spent_outputs = [&] {
        txdata.Init(tx, std::vector<CTxOut>{check.spent_outputs});
    };

    return CheckInputScriptsWithPreparedOutputs(tx, state, check.flags, cacheSigStore, cacheFullScriptStore, txdata, validation_cache, pvChecks, prepare_spent_outputs, &tx_ref);
}

Consensus::BlockSpendResult<void> CheckTransactionScriptsForBlock(const Consensus::TransactionScriptCheckPlan& check, bool cache_results, CoreScriptValidationCache& validation_cache, std::optional<CCheckQueueControl<CScriptCheck>>& control) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    bool tx_ok;
    TxValidationState tx_state;
    PrecomputedTransactionData txdata;

    // If CheckInputScripts is called with a checks vector, the checks are
    // appended and must be added to the control for asynchronous execution.
    if (control) {
        std::vector<CScriptCheck> checks;
        tx_ok = CheckInputScriptsFromPlan(check, tx_state, cache_results, cache_results, txdata, validation_cache, &checks);
        if (tx_ok) control->Add(std::move(checks));
    } else {
        tx_ok = CheckInputScriptsFromPlan(check, tx_state, cache_results, cache_results, txdata, validation_cache, nullptr);
    }
    if (!tx_ok) {
        // Any transaction validation failure during block connection is a block consensus failure.
        return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
            .issue = Consensus::BlockConsensusIssue::Consensus,
            .reject_reason = tx_state.GetRejectReason(),
            .debug_message = tx_state.GetDebugMessage(),
        }};
    }

    return {};
}

} // namespace

CoreBlockScriptChecker::CoreBlockScriptChecker(bool run_checks, bool cache_results, CoreScriptValidationCache& validation_cache, std::optional<CCheckQueueControl<CScriptCheck>>& control)
    : m_run_checks{run_checks}, m_cache_results{cache_results}, m_validation_cache{validation_cache}, m_control{control}
{
}

Consensus::BlockSpendResult<void> CoreBlockScriptChecker::Check(const Consensus::TransactionScriptCheckPlan& check)
{
    if (!m_run_checks) return {};
    return CheckTransactionScriptsForBlock(check, m_cache_results, m_validation_cache, m_control);
}

Consensus::BlockSpendResult<void> CoreBlockScriptChecker::Complete()
{
    if (!m_run_checks) return {};
    if (!m_control) return {};

    const auto parallel_result{m_control->Complete()};
    if (!parallel_result) return {};

    return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = strprintf("block-script-verify-flag-failed (%s)", ScriptErrorString(parallel_result->first)),
        .debug_message = parallel_result->second,
    }};
}

CoreBlockScriptCheckQueue::CoreBlockScriptCheckQueue(CCheckQueue<CScriptCheck>& queue, bool run_script_checks)
{
    if (queue.HasThreads() && run_script_checks) {
        m_control.emplace(queue);
    }
}

std::optional<CCheckQueueControl<CScriptCheck>>& CoreBlockScriptCheckQueue::QueueControl()
{
    return m_control;
}

CoreBlockScriptChecks::CoreBlockScriptChecks(CCheckQueue<CScriptCheck>& queue, bool run_checks, bool cache_results, ValidationCache& validation_cache)
    : m_queue{queue, run_checks}, m_validation_cache{validation_cache}, m_checker{run_checks, cache_results, m_validation_cache, m_queue.QueueControl()}
{
}

CoreBlockScriptChecker& CoreBlockScriptChecks::Checker()
{
    return m_checker;
}

bool CheckInputScripts(const CTransaction& tx, TxValidationState& state,
                       const CCoinsViewCache& inputs, script_verify_flags flags, bool cacheSigStore,
                       bool cacheFullScriptStore, PrecomputedTransactionData& txdata,
                       ValidationCache& validation_cache,
                       std::vector<CScriptCheck>* pvChecks) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    CoreScriptValidationCache script_cache{validation_cache};
    const auto prepare_spent_outputs = [&] {
        std::vector<CTxOut> spent_outputs;
        spent_outputs.reserve(tx.vin.size());

        for (const auto& txin : tx.vin) {
            const COutPoint& prevout = txin.prevout;
            const Coin& coin = inputs.AccessCoin(prevout);
            assert(!coin.IsSpent());
            spent_outputs.emplace_back(coin.out);
        }
        txdata.Init(tx, std::move(spent_outputs));
    };

    return CheckInputScriptsWithPreparedOutputs(tx, state, flags, cacheSigStore, cacheFullScriptStore, txdata, script_cache, pvChecks, prepare_spent_outputs);
}
