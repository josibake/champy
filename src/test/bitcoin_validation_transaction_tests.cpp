// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/api.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

int check(bool condition, int line) noexcept
{
    return condition ? 0 : line;
}

std::byte byte(unsigned char value) noexcept
{
    return static_cast<std::byte>(value);
}

bitcoin::script script(std::initializer_list<unsigned char> values)
{
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (auto value : values) {
        bytes.push_back(byte(value));
    }
    return bitcoin::script{bytes};
}

bitcoin::txid txid_with(std::byte value) noexcept
{
    std::array<std::byte, 32> bytes{};
    bytes.fill(value);
    return bitcoin::txid{bytes};
}

bitcoin::outpoint point(std::byte tx_byte, std::uint32_t index) noexcept
{
    return bitcoin::outpoint{txid_with(tx_byte), bitcoin::tx_output_index{index}};
}

bitcoin::tx_input input(bitcoin::outpoint previous = point(byte(1), 0), std::uint32_t sequence = 0xffffffffU)
{
    return bitcoin::tx_input{previous, script({0x51}), sequence};
}

bitcoin::tx_output output(std::int64_t value = 50)
{
    return bitcoin::tx_output{bitcoin::amount{value}, script({0x51})};
}

bitcoin::transaction tx(
    std::vector<bitcoin::tx_input> inputs = {input()},
    std::vector<bitcoin::tx_output> outputs = {output()},
    std::int32_t version = 1,
    std::uint32_t locktime = 0)
{
    return bitcoin::transaction{version, std::move(inputs), std::move(outputs), locktime};
}

bitcoin::transaction coinbase_with_script(std::initializer_list<unsigned char> script_bytes)
{
    return tx({bitcoin::tx_input{bitcoin::outpoint::null(), script(script_bytes), 0xffffffffU}}, {output()});
}

bitcoin::transaction_context context() noexcept
{
    bitcoin::transaction_context result;
    result.limits.max_money = bitcoin::amount{100};
    return result;
}

bitcoin::transaction_context tiny_size_context() noexcept
{
    auto result{context()};
    result.limits.max_stripped_weight = 1;
    return result;
}

const bitcoin::validation_rejection& rejection(const bitcoin::verify_result<bitcoin::transaction_facts>& result)
{
    return result.assume_value().assume_rejection();
}

bool accepted(const bitcoin::transaction& candidate)
{
    const auto result{bitcoin::assess_transaction_intrinsic(candidate, context())};
    return result.has_value() && result.assume_value().accepted();
}

bitcoin::validation_rule_id rejected_by(const bitcoin::transaction& candidate)
{
    const auto result{bitcoin::assess_transaction_intrinsic(candidate, context())};
    return rejection(result).rule_id();
}

bitcoin::validation_rule_id rejected_by(
    const bitcoin::transaction& candidate,
    const bitcoin::transaction_context& candidate_context)
{
    const auto result{bitcoin::assess_transaction_intrinsic(candidate, candidate_context)};
    return rejection(result).rule_id();
}

} // namespace

int main()
{
    const auto primary_valid{bitcoin::verify(tx())};
    if (auto failure{check(primary_valid.has_value() && primary_valid.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(primary_valid.assume_value().assume_facts().kind() == bitcoin::transaction_kind::ordinary, __LINE__)}) return failure;

    const auto valid{bitcoin::assess_transaction_intrinsic(tx(), context())};
    if (auto failure{check(valid.has_value() && valid.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().output_value() == bitcoin::amount{50}, __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().kind() == bitcoin::transaction_kind::ordinary, __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().stripped_weight() > 0, __LINE__)}) return failure;

    if (auto failure{check(rejected_by(tx({}, {output()})) == bitcoin::validation_rule_id::l07_transaction_inputs_non_empty, __LINE__)}) return failure;
    if (auto failure{check(rejected_by(tx({input()}, {})) == bitcoin::validation_rule_id::l08_transaction_outputs_non_empty, __LINE__)}) return failure;
    if (auto failure{check(rejected_by(tx(), tiny_size_context()) == bitcoin::validation_rule_id::l09_transaction_size, __LINE__)}) return failure;
    if (auto failure{check(rejected_by(tx({input()}, {output(-1)})) == bitcoin::validation_rule_id::l10_output_value_non_negative, __LINE__)}) return failure;
    if (auto failure{check(rejected_by(tx({input()}, {output(101)})) == bitcoin::validation_rule_id::l11_output_value_range, __LINE__)}) return failure;
    if (auto failure{check(rejected_by(tx({input()}, {output(60), output(41)})) == bitcoin::validation_rule_id::l11_output_value_range, __LINE__)}) return failure;

    const auto repeated{input(point(byte(2), 7))};
    if (auto failure{check(rejected_by(tx({repeated, repeated}, {output()})) == bitcoin::validation_rule_id::l12_unique_inputs, __LINE__)}) return failure;
    if (auto failure{check(accepted(tx({input(point(byte(2), 7)), input(point(byte(2), 8))}, {output()})), __LINE__)}) return failure;

    if (auto failure{check(accepted(coinbase_with_script({0x51, 0x51})), __LINE__)}) return failure;
    if (auto failure{check(accepted(coinbase_with_script({0x51, 0x51, 0x51})), __LINE__)}) return failure;
    if (auto failure{check(rejected_by(coinbase_with_script({0x51})) == bitcoin::validation_rule_id::l13_coinbase_script_size, __LINE__)}) return failure;

    std::vector<unsigned char> large_script(101, 0x51);
    std::vector<std::byte> large_script_bytes;
    large_script_bytes.reserve(large_script.size());
    for (auto value : large_script) {
        large_script_bytes.push_back(byte(value));
    }
    const auto oversized_coinbase{tx(
        {bitcoin::tx_input{bitcoin::outpoint::null(), bitcoin::script{large_script_bytes}, 0xffffffffU}},
        {output()})};
    if (auto failure{check(rejected_by(oversized_coinbase) == bitcoin::validation_rule_id::l13_coinbase_script_size, __LINE__)}) return failure;

    if (auto failure{check(rejected_by(tx({input(bitcoin::outpoint::null()), input(point(byte(3), 0))}, {output()})) == bitcoin::validation_rule_id::l14_non_coinbase_prevout, __LINE__)}) return failure;

    bitcoin::transaction_finality_context finality;
    finality.limits = context().limits;
    finality.height = bitcoin::block_height{10};
    finality.timestamp = bitcoin::block_time{1231006505};
    if (auto failure{check(
            rejection(bitcoin::verify(tx({input(point(byte(4), 0), 0)}, {output()}, 1, 10), finality)).rule_id() ==
                bitcoin::validation_rule_id::c01_transaction_finality,
            __LINE__)}) return failure;

    finality.height = bitcoin::block_height{11};
    if (auto failure{check(
            bitcoin::verify(tx({input(point(byte(4), 0), 0)}, {output()}, 1, 10), finality).assume_value().accepted(),
            __LINE__)}) return failure;

    finality.height = bitcoin::block_height{10};
    if (auto failure{check(
            bitcoin::verify(tx({input(point(byte(4), 0), 0xffffffffU)}, {output()}, 1, 10), finality).assume_value().accepted(),
            __LINE__)}) return failure;

    return 0;
}
