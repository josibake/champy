// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/script_task_executor.h>

#include <util/check.h>
#include <util/threadpool.h>

#include <cassert>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace validation {
namespace {

struct ScriptTaskCallable {
    ScriptTask task;

    [[nodiscard]] ScriptTaskResult operator()() { return task(); }
};

ScriptTaskResult RunScriptTasksDirect(std::vector<ScriptTask>& tasks)
{
    for (ScriptTask& task : tasks) {
        if (ScriptTaskResult result{task()}) return result;
    }
    return std::nullopt;
}

const char* SubmitErrorReason(ThreadPool::SubmitError error) noexcept
{
    switch (error) {
    case ThreadPool::SubmitError::Inactive:
        return "script thread pool is inactive";
    case ThreadPool::SubmitError::Interrupted:
        return "script thread pool is interrupted";
    }
    return "unknown script thread pool submission error";
}

Consensus::ValidationRuntimeIssue SubmitErrorRuntimeIssue(ThreadPool::SubmitError error) noexcept
{
    switch (error) {
    case ThreadPool::SubmitError::Inactive:
        return Consensus::ValidationRuntimeIssue::BackendUnavailable;
    case ThreadPool::SubmitError::Interrupted:
        return Consensus::ValidationRuntimeIssue::Cancelled;
    }
    return Consensus::ValidationRuntimeIssue::SystemError;
}

ScriptExecutionResult ScriptExecutionFailure(Consensus::ValidationRuntimeIssue runtime_issue, std::string reject_reason, std::string debug_message)
{
    return util::Unexpected{ScriptExecutionError{
        .runtime_issue = runtime_issue,
        .reject_reason = std::move(reject_reason),
        .debug_message = std::move(debug_message),
    }};
}

} // namespace

ScriptTask::ScriptTask(
    const CTxOut& output,
    CTransactionRef tx,
    SignatureCache& signature_cache,
    unsigned int input_index,
    script_verify_flags flags,
    bool cache,
    std::shared_ptr<PrecomputedTransactionData> txdata)
    : m_check{output, std::move(tx), signature_cache, input_index, flags, cache, std::move(txdata)}
{
}

ScriptTaskResult ScriptTask::operator()()
{
    return m_check();
}

ScriptExecutionResult DirectScriptTaskExecutor::Execute(std::vector<ScriptTask>&& tasks)
{
    return RunScriptTasksDirect(tasks);
}

ThreadPoolScriptTaskExecutor::ThreadPoolScriptTaskExecutor(int worker_threads)
    : m_pool{std::make_unique<ThreadPool>("script")}
{
    assert(worker_threads > 0);
    m_pool->Start(worker_threads);
}

ThreadPoolScriptTaskExecutor::~ThreadPoolScriptTaskExecutor() = default;

ScriptExecutionResult ThreadPoolScriptTaskExecutor::Execute(std::vector<ScriptTask>&& tasks)
{
    if (tasks.empty()) return ScriptTaskResult{};

    try {
        std::vector<ScriptTaskCallable> work;
        work.reserve(tasks.size());
        for (ScriptTask& task : tasks) {
            work.push_back({std::move(task)});
        }

        auto submitted{m_pool->Submit(std::move(work))};
        if (!submitted) {
            return ScriptExecutionFailure(SubmitErrorRuntimeIssue(submitted.error()), "script-task-submit-failed", SubmitErrorReason(submitted.error()));
        }

        ScriptTaskResult first_error;
        for (std::future<ScriptTaskResult>& future : *submitted) {
            ScriptTaskResult result{future.get()};
            if (result && !first_error) first_error = std::move(result);
        }
        return first_error;
    } catch (const std::exception& e) {
        return ScriptExecutionFailure(Consensus::ValidationRuntimeIssue::SystemError, "script-task-execution-failed", e.what());
    } catch (...) {
        return ScriptExecutionFailure(Consensus::ValidationRuntimeIssue::SystemError, "script-task-execution-failed", "unknown exception");
    }
}

} // namespace validation
