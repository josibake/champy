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
#include <span.h>
#include <tinyformat.h>
#include <validation/core_chain_lock.h>
#include <validation/script_validation.h>
#include <validation_state.h>

#include <cassert>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>
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
    return m_validation_cache.ContainsScriptExecution(entry, erase);
}

void CoreScriptValidationCache::StoreScriptExecution(const uint256& entry)
{
    m_validation_cache.StoreScriptExecution(entry);
}

SignatureCache& CoreScriptValidationCache::SignatureCacheStore()
{
    return m_validation_cache.m_signature_cache;
}

namespace {

template <typename ScriptCheckContainer, typename PrepareSpentOutputs>
bool CheckInputScriptsWithPreparedOutputs(
    const CTransaction& tx,
    TxValidationState& state,
    script_verify_flags flags,
    bool cacheSigStore,
    bool cacheFullScriptStore,
    PrecomputedTransactionData& txdata,
    CoreScriptValidationCache& validation_cache,
    ScriptCheckContainer* pvChecks,
    PrepareSpentOutputs&& prepare_spent_outputs,
    const CTransactionRef* tx_ref = nullptr)
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
            using Check = typename ScriptCheckContainer::value_type;
            if constexpr (std::is_same_v<Check, validation::ScriptTask>) {
                assert(tx_ref);
                pvChecks->emplace_back(txdata.m_spent_outputs[i], *tx_ref, validation_cache.SignatureCacheStore(), i, flags, cacheSigStore, queued_txdata);
            } else {
                if (tx_ref) {
                    pvChecks->emplace_back(txdata.m_spent_outputs[i], *tx_ref, validation_cache.SignatureCacheStore(), i, flags, cacheSigStore, queued_txdata);
                } else {
                    pvChecks->emplace_back(txdata.m_spent_outputs[i], tx, validation_cache.SignatureCacheStore(), i, flags, cacheSigStore, &txdata);
                }
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

bool CheckInputScriptsFromPlan(
    const Consensus::TransactionScriptCheckPlan& check,
    TxValidationState& state,
    bool cacheSigStore,
    bool cacheFullScriptStore,
    PrecomputedTransactionData& txdata,
    CoreScriptValidationCache& validation_cache,
    std::vector<validation::ScriptTask>* pvChecks)
{
    const CTransactionRef& tx_ref{check.tx};
    const CTransaction& tx{*tx_ref};
    assert(check.spent_outputs.size() == tx.vin.size());

    const auto prepare_spent_outputs = [&] {
        txdata.Init(tx, std::vector<CTxOut>{check.spent_outputs});
    };

    return CheckInputScriptsWithPreparedOutputs(tx, state, check.flags, cacheSigStore, cacheFullScriptStore, txdata, validation_cache, pvChecks, prepare_spent_outputs, &tx_ref);
}

Consensus::BlockSpendResult<void> BlockScriptError(const TxValidationState& tx_state)
{
    return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = tx_state.GetRejectReason(),
        .debug_message = tx_state.GetDebugMessage(),
    }};
}

Consensus::BlockSpendResult<void> BlockScriptTaskError(const validation::ScriptTaskError& task_error)
{
    return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = strprintf("block-script-verify-flag-failed (%s)", ScriptErrorString(task_error.first)),
        .debug_message = task_error.second,
    }};
}

Consensus::BlockSpendResult<void> BlockScriptExecutionError(validation::ScriptExecutionError error)
{
    return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::ValidationRuntime,
        .runtime_issue = error.runtime_issue,
        .reject_reason = std::move(error.reject_reason),
        .debug_message = std::move(error.debug_message),
    }};
}

Consensus::BlockSpendResult<void> ScriptPreparationExceptionError(const std::exception& e)
{
    return BlockScriptExecutionError(validation::ScriptExecutionError{
        .runtime_issue = Consensus::ValidationRuntimeIssue::SystemError,
        .reject_reason = "script-task-preparation-failed",
        .debug_message = e.what(),
    });
}

Consensus::BlockSpendResult<void> UnknownScriptPreparationExceptionError()
{
    return BlockScriptExecutionError(validation::ScriptExecutionError{
        .runtime_issue = Consensus::ValidationRuntimeIssue::SystemError,
        .reject_reason = "script-task-preparation-failed",
        .debug_message = "unknown exception",
    });
}

validation::ScriptExecutionResult ExecutorExceptionError(const std::exception& e)
{
    return util::Unexpected{validation::ScriptExecutionError{
        .runtime_issue = Consensus::ValidationRuntimeIssue::SystemError,
        .reject_reason = "script-task-execution-failed",
        .debug_message = e.what(),
    }};
}

validation::ScriptExecutionResult UnknownExecutorExceptionError()
{
    return util::Unexpected{validation::ScriptExecutionError{
        .runtime_issue = Consensus::ValidationRuntimeIssue::SystemError,
        .reject_reason = "script-task-execution-failed",
        .debug_message = "unknown exception",
    }};
}

Consensus::BlockSpendResult<void> CheckTransactionScriptsForBlock(
    const Consensus::TransactionScriptCheckPlan& check,
    bool cache_results,
    CoreScriptValidationCache& validation_cache,
    std::vector<validation::ScriptTask>* deferred_tasks)
{
    bool tx_ok;
    TxValidationState tx_state;
    PrecomputedTransactionData txdata;

    // If CheckInputScripts is called with a checks vector, the checks are
    // appended and must be added to the control for asynchronous execution.
    if (deferred_tasks) {
        tx_ok = CheckInputScriptsFromPlan(check, tx_state, cache_results, cache_results, txdata, validation_cache, deferred_tasks);
    } else {
        tx_ok = CheckInputScriptsFromPlan(
            check,
            tx_state,
            cache_results,
            cache_results,
            txdata,
            validation_cache,
            static_cast<std::vector<validation::ScriptTask>*>(nullptr));
    }
    if (!tx_ok) {
        // Any transaction validation failure during block connection is a block consensus failure.
        return BlockScriptError(tx_state);
    }

    return {};
}

} // namespace

CoreBlockScriptChecker::CoreBlockScriptChecker(bool run_checks, bool cache_results, CoreScriptValidationCache& validation_cache, validation::ScriptTaskExecutor& executor, CoreChainLock* chain_lock)
    : m_run_checks{run_checks}, m_cache_results{cache_results}, m_validation_cache{validation_cache}, m_executor{executor}, m_chain_lock{chain_lock}
{
}

Consensus::BlockSpendResult<void> CoreBlockScriptChecker::Check(const Consensus::TransactionScriptCheckPlan& check)
{
    if (!m_run_checks) return {};

    const auto check_scripts = [&]() {
        return CheckTransactionScriptsForBlock(
            check,
            m_cache_results,
            m_validation_cache,
            m_executor.ExecutesInline() ? nullptr : &m_deferred_tasks);
    };
    try {
        if (!m_chain_lock) return check_scripts();

        return m_chain_lock->RunUnlocked(check_scripts);
    } catch (const std::exception& e) {
        m_deferred_tasks.clear();
        return ScriptPreparationExceptionError(e);
    } catch (...) {
        m_deferred_tasks.clear();
        return UnknownScriptPreparationExceptionError();
    }
}

Consensus::BlockSpendResult<void> CoreBlockScriptChecker::Complete()
{
    if (!m_run_checks) return {};
    if (m_executor.ExecutesInline()) return {};

    const auto complete_checks = [&]() {
        std::vector<validation::ScriptTask> tasks;
        tasks.swap(m_deferred_tasks);
        return m_executor.Execute(std::move(tasks));
    };
    const auto parallel_result = [&] {
        try {
            if (!m_chain_lock) return complete_checks();

            return m_chain_lock->RunUnlocked(complete_checks);
        } catch (const std::exception& e) {
            return ExecutorExceptionError(e);
        } catch (...) {
            return UnknownExecutorExceptionError();
        }
    }();
    if (!parallel_result) return BlockScriptExecutionError(std::move(parallel_result).error());

    const validation::ScriptTaskResult task_result{std::move(parallel_result).value()};
    if (!task_result) return {};

    return BlockScriptTaskError(*task_result);
}

CoreBlockScriptChecks::CoreBlockScriptChecks(validation::ScriptTaskExecutor& executor, bool run_checks, bool cache_results, ValidationCache& validation_cache, CoreChainLock* chain_lock)
    : m_validation_cache{validation_cache}, m_checker{run_checks, cache_results, m_validation_cache, executor, chain_lock}
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
