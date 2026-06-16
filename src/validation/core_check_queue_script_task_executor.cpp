// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_check_queue_script_task_executor.h>

#include <optional>
#include <utility>
#include <vector>

namespace validation {
namespace {

ScriptTaskResult RunScriptTasksDirect(std::vector<ScriptTask>& tasks)
{
    for (ScriptTask& task : tasks) {
        if (ScriptTaskResult result{task()}) return result;
    }
    return std::nullopt;
}

} // namespace

bool CoreCheckQueueScriptTaskExecutor::ExecutesInline() const noexcept
{
    return !m_queue.HasThreads();
}

ScriptExecutionResult CoreCheckQueueScriptTaskExecutor::Execute(std::vector<ScriptTask>&& tasks)
{
    if (tasks.empty()) return ScriptTaskResult{};
    if (!m_queue.HasThreads()) return RunScriptTasksDirect(tasks);

    CCheckQueueControl<ScriptTask> control{m_queue};
    control.Add(std::move(tasks));
    return control.Complete();
}

} // namespace validation
