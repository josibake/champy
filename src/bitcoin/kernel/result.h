// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_KERNEL_RESULT_H
#define BITCOIN_BITCOIN_KERNEL_RESULT_H

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace bitcoin::kernel {

enum class operation_error_code {
    invalid_argument,
    exception,
    resource_exhaustion,
    io_read,
    io_write,
    data_unavailable,
    storage_corruption,
    interrupted,
    callback_failure,
    chainstate_load,
    unsupported_operation,
};

class operation_error
{
public:
    operation_error(operation_error_code code, std::string message) :
        m_code{code}, m_message{std::move(message)}
    {
    }

    [[nodiscard]] operation_error_code code() const noexcept { return m_code; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }

    friend bool operator==(const operation_error&, const operation_error&) noexcept = default;

private:
    operation_error_code m_code;
    std::string m_message;
};

enum class block_validation_result {
    unset,
    consensus,
    cached_invalid,
    invalid_header,
    mutated,
    missing_previous,
    invalid_previous,
    time_future,
    header_low_work,
};

enum class tx_validation_result {
    unset,
    consensus,
    unknown,
};

template <typename T>
class operation_result
{
public:
    [[nodiscard]] static operation_result success(T value)
    {
        return operation_result{std::move(value)};
    }

    [[nodiscard]] static operation_result failure(operation_error error)
    {
        return operation_result{std::move(error)};
    }

    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(m_value); }
    [[nodiscard]] bool has_error() const noexcept { return std::holds_alternative<operation_error>(m_value); }

    [[nodiscard]] const T& assume_value() const&
    {
        assert(has_value());
        return std::get<T>(m_value);
    }

    [[nodiscard]] T&& assume_value() &&
    {
        assert(has_value());
        return std::get<T>(std::move(m_value));
    }

    [[nodiscard]] const operation_error& assume_error() const&
    {
        assert(has_error());
        return std::get<operation_error>(m_value);
    }

    [[nodiscard]] operation_error&& assume_error() &&
    {
        assert(has_error());
        return std::get<operation_error>(std::move(m_value));
    }

private:
    explicit operation_result(T value) : m_value{std::move(value)} {}
    explicit operation_result(operation_error error) : m_value{std::move(error)} {}

    std::variant<T, operation_error> m_value;
};

template <>
class operation_result<void>
{
public:
    [[nodiscard]] static operation_result success()
    {
        return operation_result{};
    }

    [[nodiscard]] static operation_result failure(operation_error error)
    {
        return operation_result{std::move(error)};
    }

    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<std::monostate>(m_value); }
    [[nodiscard]] bool has_error() const noexcept { return std::holds_alternative<operation_error>(m_value); }

    void assume_value() const { assert(has_value()); }

    [[nodiscard]] const operation_error& assume_error() const&
    {
        assert(has_error());
        return std::get<operation_error>(m_value);
    }

    [[nodiscard]] operation_error&& assume_error() &&
    {
        assert(has_error());
        return std::get<operation_error>(std::move(m_value));
    }

private:
    operation_result() : m_value{std::monostate{}} {}
    explicit operation_result(operation_error error) : m_value{std::move(error)} {}

    std::variant<std::monostate, operation_error> m_value;
};

} // namespace bitcoin::kernel

#endif // BITCOIN_BITCOIN_KERNEL_RESULT_H
