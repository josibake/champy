// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_SCRIPT_CHECK_ADAPTERS_H
#define BITCOIN_BLOCK_SCRIPT_CHECK_ADAPTERS_H

#include <consensus/block_spend.h>
#include <script/script_check.h>
#include <validation/script_check_scheduler.h>

#include <memory>
#include <optional>

class CTransaction;
class CoreChainLock;
class SignatureCache;
class uint256;
class ValidationCache;

class CoreScriptValidationCache final
{
public:
    explicit CoreScriptValidationCache(ValidationCache& validation_cache);

    [[nodiscard]] uint256 ExecutionCacheEntry(const CTransaction& tx, script_verify_flags flags) const;
    [[nodiscard]] bool ContainsScriptExecution(const uint256& entry, bool erase);
    void StoreScriptExecution(const uint256& entry);
    [[nodiscard]] SignatureCache& SignatureCacheStore();

private:
    ValidationCache& m_validation_cache;
};

class CoreBlockScriptChecker final : public Consensus::BlockScriptChecker
{
public:
    CoreBlockScriptChecker(bool run_checks, bool cache_results, CoreScriptValidationCache& validation_cache, std::unique_ptr<validation::ScriptCheckBatch>& batch, CoreChainLock* chain_lock);

    [[nodiscard]] bool WantsChecks() const override { return m_run_checks; }
    [[nodiscard]] Consensus::BlockSpendResult<void> Check(const Consensus::TransactionScriptCheckPlan& check) override;
    [[nodiscard]] Consensus::BlockSpendResult<void> Complete() override;

private:
    bool m_run_checks;
    bool m_cache_results;
    CoreScriptValidationCache& m_validation_cache;
    std::unique_ptr<validation::ScriptCheckBatch>& m_batch;
    CoreChainLock* m_chain_lock{nullptr};
};

class CoreBlockScriptChecks final
{
public:
    CoreBlockScriptChecks(validation::ScriptCheckScheduler& scheduler, bool run_checks, bool cache_results, ValidationCache& validation_cache, CoreChainLock* chain_lock = nullptr);

    [[nodiscard]] CoreBlockScriptChecker& Checker();

private:
    std::unique_ptr<validation::ScriptCheckBatch> m_batch;
    CoreScriptValidationCache m_validation_cache;
    CoreBlockScriptChecker m_checker;
};

#endif // BITCOIN_BLOCK_SCRIPT_CHECK_ADAPTERS_H
