// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_RESULT_H
#define BITCOIN_BITCOIN_VALIDATION_RESULT_H

#include <bitcoin/protocol/result.h>
#include <bitcoin/validation/rules.h>

#include <new>
#include <string_view>
#include <utility>
#include <variant>

namespace bitcoin {

enum class validation_rejection_code {
    rule_violation,
};

class validation_rejection
{
public:
    [[nodiscard]] static constexpr validation_rejection rule(
        validation_rejection_code code,
        validation_rule_id id,
        static_text reason) noexcept
    {
        return validation_rejection{code, id, reason};
    }

    [[nodiscard]] constexpr validation_rejection_code code() const noexcept { return m_code; }
    [[nodiscard]] constexpr validation_rule_id rule_id() const noexcept { return m_rule; }
    [[nodiscard]] constexpr std::string_view rule_code() const noexcept { return validation_rule_code(m_rule); }
    [[nodiscard]] constexpr std::string_view reason() const noexcept { return m_reason.view(); }

    friend constexpr bool operator==(const validation_rejection&, const validation_rejection&) noexcept = default;

private:
    constexpr validation_rejection(
        validation_rejection_code code,
        validation_rule_id id,
        static_text reason) noexcept : m_code{code},
                                       m_rule{id},
                                       m_reason{reason}
    {
    }

    validation_rejection_code m_code;
    validation_rule_id m_rule;
    static_text m_reason;
};

enum class validation_decision_state {
    valid,
    invalid,
};

template <typename T>
class validation_decision
{
public:
    [[nodiscard]] static validation_decision valid(T facts)
    {
        return validation_decision{std::move(facts)};
    }

    [[nodiscard]] static validation_decision invalid(validation_rejection rejection)
    {
        return validation_decision{rejection};
    }

    [[nodiscard]] validation_decision_state state() const noexcept
    {
        return accepted() ? validation_decision_state::valid : validation_decision_state::invalid;
    }
    [[nodiscard]] bool accepted() const noexcept { return std::holds_alternative<T>(m_value); }
    [[nodiscard]] bool rejected() const noexcept { return state() == validation_decision_state::invalid; }
    [[nodiscard]] const T& assume_facts() const& { return std::get<T>(m_value); }
    [[nodiscard]] const validation_rejection& assume_rejection() const& { return std::get<validation_rejection>(m_value); }

private:
    explicit validation_decision(T facts) : m_value{std::move(facts)} {}
    explicit validation_decision(validation_rejection rejection) : m_value{rejection} {}

    std::variant<T, validation_rejection> m_value;
};

enum class operation_error_code {
    resource_exhaustion,
    data_unavailable,
    io_read,
    io_write,
    seek,
    storage_corruption,
    malformed_stored_data,
    interruption,
    callback_failure,
    invariant_violation,
    unsupported_operation,
    internal_bug,
};

class operation_error
{
public:
    [[nodiscard]] static constexpr operation_error make(
        operation_error_code code,
        static_text reason) noexcept
    {
        return operation_error{code, reason};
    }

    [[nodiscard]] constexpr operation_error_code code() const noexcept { return m_code; }
    [[nodiscard]] constexpr std::string_view reason() const noexcept { return m_reason.view(); }

    friend constexpr bool operator==(const operation_error&, const operation_error&) noexcept = default;

private:
    constexpr operation_error(operation_error_code code, static_text reason) noexcept : m_code{code},
                                                                                        m_reason{reason}
    {
    }

    operation_error_code m_code;
    static_text m_reason;
};

enum class operation_result_state {
    value,
    error,
};

template <typename T>
class operation_result
{
public:
    [[nodiscard]] static operation_result ok(T value)
    {
        return operation_result{std::move(value)};
    }

    [[nodiscard]] static operation_result failed(operation_error error)
    {
        return operation_result{error};
    }

    [[nodiscard]] operation_result_state state() const noexcept
    {
        return has_value() ? operation_result_state::value : operation_result_state::error;
    }
    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(m_value); }
    [[nodiscard]] bool has_error() const noexcept { return state() == operation_result_state::error; }
    [[nodiscard]] const T& assume_value() const& { return std::get<T>(m_value); }
    [[nodiscard]] T&& assume_value() && { return std::get<T>(std::move(m_value)); }
    [[nodiscard]] const operation_error& assume_error() const& { return std::get<operation_error>(m_value); }

private:
    explicit operation_result(T value) : m_value{std::move(value)} {}
    explicit operation_result(operation_error error) : m_value{error} {}

    std::variant<T, operation_error> m_value;
};

template <typename T>
using verify_result = operation_result<validation_decision<T>>;

template <typename T, typename Function>
[[nodiscard]] operation_result<T> operation_exception_boundary(
    Function&& function,
    static_text resource_reason,
    operation_error_code unexpected_code,
    static_text unexpected_reason)
{
    try {
        return std::forward<Function>(function)();
    } catch (const std::bad_alloc&) {
        return operation_result<T>::failed(operation_error::make(
            operation_error_code::resource_exhaustion,
            resource_reason));
    } catch (...) {
        return operation_result<T>::failed(operation_error::make(
            unexpected_code,
            unexpected_reason));
    }
}

template <typename T, typename Function>
[[nodiscard]] verify_result<T> verify_exception_boundary(
    Function&& function,
    static_text resource_reason,
    operation_error_code unexpected_code,
    static_text unexpected_reason)
{
    try {
        return std::forward<Function>(function)();
    } catch (const std::bad_alloc&) {
        return verify_result<T>::failed(operation_error::make(
            operation_error_code::resource_exhaustion,
            resource_reason));
    } catch (...) {
        return verify_result<T>::failed(operation_error::make(
            unexpected_code,
            unexpected_reason));
    }
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_RESULT_H
