// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_KERNEL_CONTEXT_H
#define BITCOIN_BITCOIN_KERNEL_CONTEXT_H

#include <bitcoin/kernel/events.h>
#include <bitcoin/kernel/result.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

namespace bitcoin::kernel {

enum class chain_type {
    mainnet,
    testnet,
    testnet4,
    signet,
    regtest,
};

class chain_parameters
{
public:
    constexpr explicit chain_parameters(chain_type type = chain_type::mainnet) noexcept : m_type{type} {}

    [[nodiscard]] constexpr chain_type type() const noexcept { return m_type; }

    friend constexpr bool operator==(const chain_parameters&, const chain_parameters&) noexcept = default;

private:
    chain_type m_type;
};

class operation_time
{
public:
    constexpr operation_time() noexcept = default;
    constexpr explicit operation_time(std::chrono::sys_seconds value) noexcept : m_value{value} {}

    [[nodiscard]] static constexpr operation_time from_unix_seconds(std::int64_t seconds) noexcept
    {
        return operation_time{std::chrono::sys_seconds{std::chrono::seconds{seconds}}};
    }

    [[nodiscard]] constexpr std::chrono::sys_seconds value() const noexcept { return m_value; }
    [[nodiscard]] constexpr std::int64_t unix_seconds() const noexcept { return m_value.time_since_epoch().count(); }

    friend constexpr bool operator==(const operation_time&, const operation_time&) noexcept = default;
    friend constexpr auto operator<=>(const operation_time&, const operation_time&) noexcept = default;

private:
    std::chrono::sys_seconds m_value{};
};

class context_options
{
public:
    context_options() = default;
    explicit context_options(chain_parameters params) noexcept : m_chain_params{params} {}

    [[nodiscard]] const chain_parameters& parameters() const noexcept { return m_chain_params; }
    void set_chain_parameters(chain_parameters params) noexcept { m_chain_params = params; }

    [[nodiscard]] const diagnostic_sink& diagnostics() const noexcept { return m_diagnostics; }
    void set_diagnostics(diagnostic_sink diagnostics) { m_diagnostics = std::move(diagnostics); }

    [[nodiscard]] const event_sink& events() const noexcept { return m_events; }
    void set_events(event_sink events) { m_events = std::move(events); }

private:
    chain_parameters m_chain_params{};
    diagnostic_sink m_diagnostics;
    event_sink m_events;
};

class context
{
public:
    // Move-only runtime owner. A moved-from context is valid only for
    // destruction or assignment.
    context(context&&) noexcept;
    context& operator=(context&&) noexcept;
    context(const context&) = delete;
    context& operator=(const context&) = delete;
    ~context();

    [[nodiscard]] const chain_parameters& parameters() const noexcept;
    [[nodiscard]] bool interrupt_requested() const noexcept;

private:
    friend operation_result<context> make_context(context_options options);
    friend operation_result<void> request_interrupt(context& ctx);

    struct state;

    explicit context(std::unique_ptr<state> value) noexcept;

    std::unique_ptr<state> m_state;
};

[[nodiscard]] operation_result<context> make_context(context_options options);

// On success, ctx records an interrupt request. If event delivery fails, the
// interrupt request remains recorded and the callback failure is reported.
[[nodiscard]] operation_result<void> request_interrupt(context& ctx);

} // namespace bitcoin::kernel

#endif // BITCOIN_BITCOIN_KERNEL_CONTEXT_H
