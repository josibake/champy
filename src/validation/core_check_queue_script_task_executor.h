// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_CHECK_QUEUE_SCRIPT_TASK_EXECUTOR_H
#define BITCOIN_VALIDATION_CORE_CHECK_QUEUE_SCRIPT_TASK_EXECUTOR_H

#include <checkqueue.h>
#include <validation/script_task_executor.h>

#include <vector>

namespace validation {

class CoreCheckQueueScriptTaskExecutor final : public ScriptTaskExecutor
{
public:
    explicit CoreCheckQueueScriptTaskExecutor(CCheckQueue<ScriptTask>& queue) : m_queue{queue} {}

    [[nodiscard]] bool ExecutesInline() const noexcept override;
    [[nodiscard]] ScriptExecutionResult Execute(std::vector<ScriptTask>&& tasks) override;

private:
    CCheckQueue<ScriptTask>& m_queue;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_CORE_CHECK_QUEUE_SCRIPT_TASK_EXECUTOR_H
