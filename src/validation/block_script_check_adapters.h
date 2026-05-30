// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_SCRIPT_CHECK_ADAPTERS_H
#define BITCOIN_BLOCK_SCRIPT_CHECK_ADAPTERS_H

#include <checkqueue.h>
#include <consensus/block_spend.h>
#include <kernel/cs_main.h>
#include <script/script_check.h>

#include <optional>

class CTransaction;
class SignatureCache;
class uint256;
class ValidationCache;

class CoreScriptValidationCache final
{
public:
    explicit CoreScriptValidationCache(ValidationCache& validation_cache);

    [[nodiscard]] uint256 ExecutionCacheEntry(const CTransaction& tx, script_verify_flags flags) const;
    [[nodiscard]] bool ContainsScriptExecution(const uint256& entry, bool erase) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void StoreScriptExecution(const uint256& entry) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] SignatureCache& SignatureCacheStore();

private:
    ValidationCache& m_validation_cache;
};

class CoreBlockScriptChecker final : public Consensus::BlockScriptChecker
{
public:
    CoreBlockScriptChecker(bool run_checks, bool cache_results, CoreScriptValidationCache& validation_cache, std::optional<CCheckQueueControl<CScriptCheck>>& control);

    [[nodiscard]] bool WantsChecks() const override { return m_run_checks; }
    [[nodiscard]] Consensus::BlockSpendResult<void> Check(const Consensus::TransactionScriptCheckPlan& check) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] Consensus::BlockSpendResult<void> Complete() override;

private:
    bool m_run_checks;
    bool m_cache_results;
    CoreScriptValidationCache& m_validation_cache;
    std::optional<CCheckQueueControl<CScriptCheck>>& m_control;
};

class CoreBlockScriptCheckQueue final
{
public:
    CoreBlockScriptCheckQueue(CCheckQueue<CScriptCheck>& queue, bool run_script_checks);

    [[nodiscard]] std::optional<CCheckQueueControl<CScriptCheck>>& QueueControl();

private:
    std::optional<CCheckQueueControl<CScriptCheck>> m_control;
};

class CoreBlockScriptChecks final
{
public:
    CoreBlockScriptChecks(CCheckQueue<CScriptCheck>& queue, bool run_checks, bool cache_results, ValidationCache& validation_cache);

    [[nodiscard]] CoreBlockScriptChecker& Checker();

private:
    CoreBlockScriptCheckQueue m_queue;
    CoreScriptValidationCache m_validation_cache;
    CoreBlockScriptChecker m_checker;
};

#endif // BITCOIN_BLOCK_SCRIPT_CHECK_ADAPTERS_H
