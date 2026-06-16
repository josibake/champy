// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <bitcoin/validation/api.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

bitcoin::script script(std::initializer_list<unsigned char> values)
{
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (const auto value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bitcoin::script{bytes};
}

} // namespace

int main()
{
    const auto previous_script{script({0x51})};
    const auto spent_amount{bitcoin::amount{50}};
    const bitcoin::transaction spending_transaction{
        1,
        {bitcoin::tx_input{
            bitcoin::outpoint{bitcoin::txid{}, bitcoin::tx_output_index{0}},
            script({0x51}),
            0xffffffffU}},
        {bitcoin::tx_output{bitcoin::amount{1}, script({0x51})}},
        0};
    const std::vector<bitcoin::tx_output> prevouts{
        bitcoin::tx_output{spent_amount, previous_script}};

    const auto result{bitcoin::verify_script(
        previous_script,
        spent_amount,
        spending_transaction,
        0,
        bitcoin::verification_flags::none(),
        prevouts)};

    return result.accepted() ? 0 : 1;
}
