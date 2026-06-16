// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/protocol/result.h>

#include <concepts>
#include <string_view>
#include <type_traits>

namespace {

struct value {
    int n;

    friend bool operator==(value, value) noexcept = default;
};

static_assert(!std::default_initializable<bitcoin::malformed_parse>);
static_assert(!std::default_initializable<bitcoin::parse_result<value>>);

} // namespace

int main()
{
    const auto parsed{bitcoin::parse_result<value>::parsed(value{7})};
    if (parsed.state() != bitcoin::parse_result_state::parsed || !parsed || parsed.has_failure() ||
        parsed.assume_value().n != 7) {
        return 1;
    }

    const auto malformed{bitcoin::parse_result<value>::malformed(
        bitcoin::malformed_parse{bitcoin::parse_failure_code::trailing_data, 80})};
    if (malformed.state() != bitcoin::parse_result_state::malformed || malformed || !malformed.has_failure()) {
        return 1;
    }
    if (malformed.assume_failure().code() != bitcoin::parse_failure_code::trailing_data) {
        return 1;
    }
    if (malformed.assume_failure().offset() != 80) {
        return 1;
    }

    return 0;
}
