// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_SCRIPT_TASK_EXECUTOR_H
#define BITCOIN_VALIDATION_SCRIPT_TASK_EXECUTOR_H

#include <consensus/diagnostics.h>
#include <script/script_check.h>
#include <util/expected.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool;

namespace validation {

using ScriptTaskError = std::remove_cvref_t<decltype(std::declval<CScriptCheck>()().value())>;
using ScriptTaskResult = std::optional<ScriptTaskError>;

struct ScriptExecutionError {
    Consensus::ValidationRuntimeIssue runtime_issue{Consensus::ValidationRuntimeIssue::SystemError};
    std::string reject_reason;
    std::string debug_message;
};

using ScriptExecutionResult = util::Expected<ScriptTaskResult, ScriptExecutionError>;

class ScriptTask
{
public:
    ScriptTask(
        const CTxOut& output,
        CTransactionRef tx,
        SignatureCache& signature_cache,
        unsigned int input_index,
        script_verify_flags flags,
        bool cache,
        std::shared_ptr<PrecomputedTransactionData> txdata);
    ScriptTask(
        const CTxOut&,
        const CTransaction&,
        SignatureCache&,
        unsigned int,
        script_verify_flags,
        bool,
        PrecomputedTransactionData*) = delete;

    ScriptTask(const ScriptTask&) = delete;
    ScriptTask& operator=(const ScriptTask&) = delete;
    ScriptTask(ScriptTask&&) noexcept = default;
    ScriptTask& operator=(ScriptTask&&) noexcept = default;

    [[nodiscard]] ScriptTaskResult operator()();

private:
    CScriptCheck m_check;
};

/**
 * Runtime-owned executor for prepared script validation work.
 *
 * Consensus code emits value work. The executor owns how that work runs:
 * directly, through a validation thread pool, or through a legacy Core queue.
 */
class ScriptTaskExecutor
{
public:
    virtual ~ScriptTaskExecutor() = default;

    [[nodiscard]] virtual bool ExecutesInline() const noexcept = 0;
    [[nodiscard]] virtual ScriptExecutionResult Execute(std::vector<ScriptTask>&& tasks) = 0;
};

class DirectScriptTaskExecutor final : public ScriptTaskExecutor
{
public:
    [[nodiscard]] bool ExecutesInline() const noexcept override { return true; }
    [[nodiscard]] ScriptExecutionResult Execute(std::vector<ScriptTask>&& tasks) override;
};

class ThreadPoolScriptTaskExecutor final : public ScriptTaskExecutor
{
public:
    explicit ThreadPoolScriptTaskExecutor(int worker_threads);
    ~ThreadPoolScriptTaskExecutor();

    ThreadPoolScriptTaskExecutor(const ThreadPoolScriptTaskExecutor&) = delete;
    ThreadPoolScriptTaskExecutor& operator=(const ThreadPoolScriptTaskExecutor&) = delete;

    [[nodiscard]] bool ExecutesInline() const noexcept override { return false; }
    [[nodiscard]] ScriptExecutionResult Execute(std::vector<ScriptTask>&& tasks) override;

private:
    std::unique_ptr<ThreadPool> m_pool;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_SCRIPT_TASK_EXECUTOR_H
