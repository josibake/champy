// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/script_check_scheduler.h>

#include <utility>

namespace validation {
namespace {

class CCheckQueueScriptCheckBatch final : public ScriptCheckBatch
{
public:
    explicit CCheckQueueScriptCheckBatch(CCheckQueue<CScriptCheck>& queue) : m_control{queue} {}

    void Add(std::vector<CScriptCheck>&& checks) override
    {
        m_control.Add(std::move(checks));
    }

    ScriptCheckResult Complete() override
    {
        return m_control.Complete();
    }

private:
    CCheckQueueControl<CScriptCheck> m_control;
};

} // namespace

std::unique_ptr<ScriptCheckBatch> CCheckQueueScriptCheckScheduler::StartBatch(bool run_script_checks)
{
    if (!run_script_checks || !m_queue.HasThreads()) return nullptr;
    return std::make_unique<CCheckQueueScriptCheckBatch>(m_queue);
}

} // namespace validation
