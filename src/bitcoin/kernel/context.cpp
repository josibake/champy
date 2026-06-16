// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/kernel/context.h>

#include <cassert>
#include <exception>
#include <memory>
#include <utility>

namespace bitcoin::kernel {

namespace {
operation_error callback_failure(std::string message)
{
    return operation_error{operation_error_code::callback_failure, std::move(message)};
}
} // namespace

operation_result<void> diagnostic_sink::emit(const diagnostic_event& event) const
{
    if (!m_callback) {
        return operation_result<void>::success();
    }
    try {
        if (m_callback(event) == callback_status::ok) {
            return operation_result<void>::success();
        }
        return operation_result<void>::failure(callback_failure("diagnostic callback returned failure"));
    } catch (const std::exception& exception) {
        return operation_result<void>::failure(callback_failure(exception.what()));
    } catch (...) {
        return operation_result<void>::failure(callback_failure("diagnostic callback threw a non-standard exception"));
    }
}

operation_result<void> event_sink::emit(const kernel_event& event) const
{
    if (!m_callback) {
        return operation_result<void>::success();
    }
    try {
        if (m_callback(event) == callback_status::ok) {
            return operation_result<void>::success();
        }
        return operation_result<void>::failure(callback_failure("event callback returned failure"));
    } catch (const std::exception& exception) {
        return operation_result<void>::failure(callback_failure(exception.what()));
    } catch (...) {
        return operation_result<void>::failure(callback_failure("event callback threw a non-standard exception"));
    }
}

struct context::state {
    chain_parameters parameters;
    diagnostic_sink diagnostics;
    event_sink events;
    bool interrupt_requested{false};
};

context::context(std::unique_ptr<state> value) noexcept : m_state{std::move(value)} {}
context::context(context&&) noexcept = default;
context& context::operator=(context&&) noexcept = default;
context::~context() = default;

const chain_parameters& context::parameters() const noexcept
{
    assert(m_state);
    return m_state->parameters;
}

bool context::interrupt_requested() const noexcept
{
    assert(m_state);
    return m_state->interrupt_requested;
}

operation_result<context> make_context(context_options options)
{
    auto state{std::make_unique<context::state>()};
    state->parameters = options.parameters();
    state->diagnostics = options.diagnostics();
    state->events = options.events();

    if (auto result = state->diagnostics.emit(diagnostic_event{
            .category = diagnostic_category::kernel,
            .level = diagnostic_level::debug,
            .message = "kernel context created"}); result.has_error()) {
        return operation_result<context>::failure(std::move(result).assume_error());
    }
    if (auto result = state->events.emit(kernel_event{
            .code = kernel_event_code::context_created,
            .message = "kernel context created"}); result.has_error()) {
        return operation_result<context>::failure(std::move(result).assume_error());
    }

    return operation_result<context>::success(context{std::move(state)});
}

operation_result<void> request_interrupt(context& ctx)
{
    assert(ctx.m_state);
    ctx.m_state->interrupt_requested = true;
    return ctx.m_state->events.emit(kernel_event{
        .code = kernel_event_code::interrupt_requested,
        .message = "kernel interrupt requested"});
}

} // namespace bitcoin::kernel
