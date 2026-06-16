// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_KERNEL_EVENTS_H
#define BITCOIN_BITCOIN_KERNEL_EVENTS_H

#include <bitcoin/kernel/result.h>

#include <functional>
#include <string>
#include <utility>

namespace bitcoin::kernel {

enum class diagnostic_category {
    kernel,
    storage,
    validation,
};

enum class diagnostic_level {
    trace,
    debug,
    info,
    warning,
    error,
};

struct diagnostic_event {
    diagnostic_category category{diagnostic_category::kernel};
    diagnostic_level level{diagnostic_level::info};
    std::string message;

    friend bool operator==(const diagnostic_event&, const diagnostic_event&) noexcept = default;
};

enum class callback_status {
    ok,
    failed,
};

class diagnostic_sink
{
public:
    using callback_type = std::function<callback_status(const diagnostic_event&)>;

    diagnostic_sink() = default;
    explicit diagnostic_sink(callback_type callback) : m_callback{std::move(callback)} {}

    [[nodiscard]] operation_result<void> emit(const diagnostic_event& event) const;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_callback); }

private:
    callback_type m_callback;
};

class diagnostic_connection
{
public:
    diagnostic_connection() = default;
    explicit diagnostic_connection(diagnostic_sink sink) : m_sink{std::move(sink)} {}

    [[nodiscard]] const diagnostic_sink& sink() const noexcept { return m_sink; }

private:
    diagnostic_sink m_sink;
};

enum class synchronization_state {
    init_reindex,
    init_download,
    post_init,
};

enum class warning {
    unknown_new_rules_activated,
    large_work_invalid_chain,
};

enum class kernel_event_code {
    context_created,
    interrupt_requested,
};

struct kernel_event {
    kernel_event_code code{kernel_event_code::context_created};
    std::string message;

    friend bool operator==(const kernel_event&, const kernel_event&) noexcept = default;
};

class event_sink
{
public:
    using callback_type = std::function<callback_status(const kernel_event&)>;

    event_sink() = default;
    explicit event_sink(callback_type callback) : m_callback{std::move(callback)} {}

    [[nodiscard]] operation_result<void> emit(const kernel_event& event) const;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_callback); }

private:
    callback_type m_callback;
};

} // namespace bitcoin::kernel

#endif // BITCOIN_BITCOIN_KERNEL_EVENTS_H
