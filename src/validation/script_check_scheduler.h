// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_SCRIPT_CHECK_SCHEDULER_H
#define BITCOIN_VALIDATION_SCRIPT_CHECK_SCHEDULER_H

#include <checkqueue.h>
#include <script/script_check.h>

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace validation {

using ScriptCheckError = std::remove_cvref_t<decltype(std::declval<CScriptCheck>()().value())>;
using ScriptCheckResult = std::optional<ScriptCheckError>;

class ScriptCheckBatch
{
public:
    virtual ~ScriptCheckBatch() = default;

    virtual void Add(std::vector<CScriptCheck>&& checks) = 0;
    [[nodiscard]] virtual ScriptCheckResult Complete() = 0;
};

class ScriptCheckScheduler
{
public:
    virtual ~ScriptCheckScheduler() = default;

    [[nodiscard]] virtual std::unique_ptr<ScriptCheckBatch> StartBatch(bool run_script_checks) = 0;
};

class CCheckQueueScriptCheckScheduler final : public ScriptCheckScheduler
{
public:
    explicit CCheckQueueScriptCheckScheduler(CCheckQueue<CScriptCheck>& queue) : m_queue{queue} {}

    [[nodiscard]] std::unique_ptr<ScriptCheckBatch> StartBatch(bool run_script_checks) override;

private:
    CCheckQueue<CScriptCheck>& m_queue;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_SCRIPT_CHECK_SCHEDULER_H
