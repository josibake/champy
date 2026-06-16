// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/kernel/api.h>

#include <chrono>
#include <concepts>
#include <filesystem>
#include <string>
#include <utility>

namespace {

int require(bool value)
{
    return value ? 0 : 1;
}

template <typename T>
concept contextually_bool_testable = requires(T value) {
    value ? 0 : 1;
};

static_assert(!contextually_bool_testable<bitcoin::kernel::operation_result<int>>);
static_assert(!contextually_bool_testable<bitcoin::kernel::operation_result<void>>);

} // namespace

int main()
{
    namespace kernel = bitcoin::kernel;

    int failures{0};
    int event_count{0};
    int diagnostic_count{0};

    kernel::context_options options{kernel::chain_parameters{kernel::chain_type::regtest}};
    options.set_events(kernel::event_sink{[&](const kernel::kernel_event& event) {
        ++event_count;
        return event.message.empty() ? kernel::callback_status::failed : kernel::callback_status::ok;
    }});
    options.set_diagnostics(kernel::diagnostic_sink{[&](const kernel::diagnostic_event& event) {
        ++diagnostic_count;
        return event.level == kernel::diagnostic_level::debug ? kernel::callback_status::ok : kernel::callback_status::failed;
    }});

    auto context_result{kernel::make_context(std::move(options))};
    failures += require(context_result.has_value());
    auto context{std::move(context_result).assume_value()};
    failures += require(context.parameters().type() == kernel::chain_type::regtest);
    failures += require(event_count == 1);
    failures += require(diagnostic_count == 1);

    const auto operation_time{kernel::operation_time::from_unix_seconds(1'700'000'000)};
    failures += require(operation_time.unix_seconds() == 1'700'000'000);

    auto interrupt_result{kernel::request_interrupt(context)};
    failures += require(interrupt_result.has_value());
    failures += require(context.interrupt_requested());
    failures += require(event_count == 2);

    auto chainstate_options_result{kernel::chainstate_options::from_directories(
        std::filesystem::path{"/tmp/champy-kernel-data"},
        std::filesystem::path{"/tmp/champy-kernel-blocks"})};
    failures += require(chainstate_options_result.has_value());
    auto chainstate_options{std::move(chainstate_options_result).assume_value()};
    chainstate_options.set_in_memory(true);
    chainstate_options.set_wipe_chainstate(true);
    failures += require(chainstate_options.in_memory());
    failures += require(chainstate_options.wipe_chainstate());

    auto open_result{kernel::open_chainstate(
        context,
        chainstate_options,
        kernel::chainstate_runtime{operation_time})};
    failures += require(open_result.has_error());
    failures += require(open_result.assume_error().code() == kernel::operation_error_code::interrupted);

    kernel::context_options failing_options{};
    failing_options.set_events(kernel::event_sink{[](const kernel::kernel_event&) {
        return kernel::callback_status::failed;
    }});
    auto failing_context{kernel::make_context(std::move(failing_options))};
    failures += require(failing_context.has_error());
    failures += require(failing_context.assume_error().code() == kernel::operation_error_code::callback_failure);

    auto invalid_options{kernel::chainstate_options::from_directories(
        std::filesystem::path{},
        std::filesystem::path{"/tmp/champy-kernel-blocks"})};
    failures += require(invalid_options.has_error());
    failures += require(invalid_options.assume_error().code() == kernel::operation_error_code::invalid_argument);

    kernel::block_read_result not_indexed{kernel::block_read_result::not_indexed()};
    failures += require(not_indexed.status() == kernel::block_read_status::not_indexed);
    failures += require(!not_indexed.block().has_value());

    kernel::block_spent_outputs_read_result unavailable{kernel::block_spent_outputs_read_result::data_unavailable()};
    failures += require(unavailable.status() == kernel::block_spent_outputs_read_status::data_unavailable);
    failures += require(!unavailable.spent_outputs().has_value());

    return failures == 0 ? 0 : 1;
}
