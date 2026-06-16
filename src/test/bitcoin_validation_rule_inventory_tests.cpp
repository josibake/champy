// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/rules.h>

#include <array>
#include <string_view>

namespace {

int check(bool condition, int line) noexcept
{
    return condition ? 0 : line;
}

constexpr std::array expected_codes{
    "H01", "H02", "H03", "H04", "H05", "H06", "H07",
    "L01", "L02", "L03", "L04", "L05", "L06", "L07", "L08", "L09", "L10", "L11", "L12", "L13", "L14",
    "C01", "C02", "C03", "C04", "C05", "C06", "C07",
    "S01", "S02", "S03", "S04", "S05", "S06", "S07", "S08", "S09"};

} // namespace

int main()
{
    if (auto failure{check(bitcoin::validation_rule_count == expected_codes.size(), __LINE__)}) return failure;
    if (auto failure{check(bitcoin::validation_rule_inventory.size() == expected_codes.size(), __LINE__)}) return failure;

    for (std::size_t i{0}; i < expected_codes.size(); ++i) {
        const auto& rule{bitcoin::validation_rule_inventory[i]};
        if (auto failure{check(rule.code() == std::string_view{expected_codes[i]}, __LINE__)}) return failure;
        if (auto failure{check(!rule.statement().empty(), __LINE__)}) return failure;
        if (auto failure{check(bitcoin::find_validation_rule(rule.id()) == &rule, __LINE__)}) return failure;
        if (auto failure{check(bitcoin::validation_rule_code(rule.id()) == rule.code(), __LINE__)}) return failure;
    }

    return 0;
}
