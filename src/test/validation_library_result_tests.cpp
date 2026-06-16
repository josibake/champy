// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/header.h>
#include <bitcoin/validation/result.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

struct facts {
    int value;

    friend bool operator==(facts, facts) noexcept = default;
};

static_assert(!std::default_initializable<bitcoin::validation_rejection>);
static_assert(!std::default_initializable<bitcoin::operation_error>);

using decision = bitcoin::validation_decision<facts>;
using result = bitcoin::operation_result<decision>;

template <typename T>
concept contextually_bool_testable = requires(T value) {
    value ? 1 : 0;
};

static_assert(!std::default_initializable<decision>);
static_assert(!std::default_initializable<result>);
static_assert(!contextually_bool_testable<decision>);
static_assert(!contextually_bool_testable<result>);
static_assert(!contextually_bool_testable<bitcoin::header_context_result>);
static_assert(!contextually_bool_testable<bitcoin::evidence_verify_result<facts>>);
static_assert(std::same_as<bitcoin::verify_result<facts>, bitcoin::operation_result<bitcoin::validation_decision<facts>>>);

} // namespace

int main()
{
    const auto rejection{bitcoin::validation_rejection::rule(
        bitcoin::validation_rejection_code::rule_violation,
        bitcoin::validation_rule_id::h05_future_time,
        "test reason")};
    if (rejection.rule_id() != bitcoin::validation_rule_id::h05_future_time) {
        return 1;
    }
    if (rejection.rule_code() != std::string_view{"H05"}) {
        return 1;
    }

    const auto invalid{decision::invalid(rejection)};
    if (invalid.state() != bitcoin::validation_decision_state::invalid || invalid.accepted() || !invalid.rejected()) {
        return 1;
    }
    if (invalid.assume_rejection().reason() != std::string_view{"test reason"}) {
        return 1;
    }

    const auto accepted{decision::valid(facts{42})};
    if (accepted.state() != bitcoin::validation_decision_state::valid || !accepted.accepted() || accepted.rejected() || accepted.assume_facts().value != 42) {
        return 1;
    }

    const auto ok{result::ok(accepted)};
    if (ok.state() != bitcoin::operation_result_state::value || !ok.has_value() || ok.has_error() || !ok.assume_value().accepted()) {
        return 1;
    }

    const auto failed{result::failed(bitcoin::operation_error::make(
        bitcoin::operation_error_code::interruption,
        "stop requested"))};
    if (failed.state() != bitcoin::operation_result_state::error || failed.has_value() || !failed.has_error() ||
        failed.assume_error().code() != bitcoin::operation_error_code::interruption) {
        return 1;
    }

    const auto unavailable{result::failed(bitcoin::operation_error::make(
        bitcoin::operation_error_code::data_unavailable,
        "data unavailable"))};
    if (unavailable.has_value() || unavailable.assume_error().code() != bitcoin::operation_error_code::data_unavailable) {
        return 1;
    }

    return 0;
}
