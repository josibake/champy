// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
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

bitcoin::witness_item witness_item(std::initializer_list<unsigned char> values)
{
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (auto value : values) {
        bytes.push_back(byte(value));
    }
    return bitcoin::witness_item{bytes};
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

bitcoin::tx_input input(bitcoin::outpoint previous = point(byte(1), 0))
{
    return bitcoin::tx_input{previous, script({0x51}), 0xffffffffU};
}

bitcoin::tx_output output(std::int64_t value = 50)
{
    return bitcoin::tx_output{bitcoin::amount{value}, script({0x51})};
}

bitcoin::transaction tx(
    std::vector<bitcoin::tx_input> inputs = {input()},
    std::vector<bitcoin::tx_output> outputs = {output()})
{
    return bitcoin::transaction{1, std::move(inputs), std::move(outputs), 0};
}

bitcoin::transaction coinbase_tx()
{
    return bitcoin::transaction{
        1,
        {bitcoin::tx_input{bitcoin::outpoint::null(), script({0x51, 0x51}), 0xffffffffU}},
        {output()},
        0};
}

bitcoin::median_time_past mtp(std::int64_t seconds) noexcept
{
    return bitcoin::median_time_past{std::chrono::sys_seconds{std::chrono::seconds{seconds}}};
}

bitcoin::coin coin(std::int64_t value)
{
    return bitcoin::coin{
        output(value),
        bitcoin::block_height{100},
        false,
        mtp(1231006505)};
}

bitcoin::coin coin_with_script(std::int64_t value, bitcoin::script script_pubkey)
{
    return bitcoin::coin{
        bitcoin::tx_output{bitcoin::amount{value}, std::move(script_pubkey)},
        bitcoin::block_height{100},
        false,
        mtp(1231006505)};
}

bitcoin::spend_context spend_context() noexcept
{
    bitcoin::spend_context result;
    result.height = bitcoin::block_height{200};
    result.previous_median_time_past = mtp(1231007000);
    result.limits.max_money = bitcoin::amount{100};
    result.coinbase_maturity = 100;
    return result;
}

bitcoin::script_context script_context() noexcept
{
    return bitcoin::script_context{
        bitcoin::verification_flags::p2sh() |
        bitcoin::verification_flags::dersig() |
        bitcoin::verification_flags::witness()};
}

class in_memory_coin_index
{
public:
    void set(bitcoin::outpoint point, bitcoin::coin value)
    {
        m_entries.push_back(entry{point, std::move(value)});
    }

    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint& point) const
    {
        const auto match{std::ranges::find_if(m_entries, [&](const entry& candidate) {
            return candidate.point == point;
        })};
        if (match == m_entries.end()) {
            return std::nullopt;
        }
        return match->value;
    }

private:
    struct entry {
        bitcoin::outpoint point;
        bitcoin::coin value;
    };

    std::vector<entry> m_entries;
};

static_assert(bitcoin::coin_index<in_memory_coin_index>);

class throwing_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint&) const
    {
        throw std::runtime_error{"coin lookup failed"};
    }
};

static_assert(bitcoin::coin_index<throwing_coin_index>);

static_assert(bitcoin::verification_flags::none().mask() == 0);
static_assert(bitcoin::verification_flags::p2sh().mask() == (1ULL << 0));
static_assert(bitcoin::verification_flags::dersig().mask() == (1ULL << 2));
static_assert(bitcoin::verification_flags::nulldummy().mask() == (1ULL << 4));
static_assert(bitcoin::verification_flags::checklocktimeverify().mask() == (1ULL << 9));
static_assert(bitcoin::verification_flags::checksequenceverify().mask() == (1ULL << 10));
static_assert(bitcoin::verification_flags::witness().mask() == (1ULL << 11));
static_assert(bitcoin::verification_flags::taproot().mask() == (1ULL << 17));
static_assert(bitcoin::verification_flags::all().contains(bitcoin::verification_flags::p2sh()));
static_assert(bitcoin::verification_flags::all().contains(bitcoin::verification_flags::taproot()));
static_assert(bitcoin::verification_flags::from_mask(bitcoin::verification_flags::all().mask()).has_value());
static_assert(!bitcoin::verification_flags::from_mask(1ULL << 63).has_value());

} // namespace

int main()
{
    in_memory_coin_index index;
    index.set(point(byte(1), 0), coin(60));

    const bitcoin::tx_output accepted_prevout{bitcoin::amount{60}, script({0x51})};
    const std::array accepted_prevouts{accepted_prevout};
    const auto accepted_script{bitcoin::verify_script(
        accepted_prevout.script(),
        accepted_prevout.value(),
        tx(),
        0,
        bitcoin::verification_flags::none(),
        std::span<const bitcoin::tx_output>{accepted_prevouts})};
    if (auto failure{check(accepted_script.accepted(), __LINE__)}) return failure;

    const bitcoin::tx_output rejected_prevout{bitcoin::amount{60}, script({0x00})};
    const std::array rejected_prevouts{rejected_prevout};
    const auto rejected_script{bitcoin::verify_script(
        rejected_prevout.script(),
        rejected_prevout.value(),
        tx(),
        0,
        bitcoin::verification_flags::none(),
        std::span<const bitcoin::tx_output>{rejected_prevouts})};
    if (auto failure{check(rejected_script.rejected(), __LINE__)}) return failure;

    const bitcoin::tx_output non_push_prevout{bitcoin::amount{60}, script({0x51, 0x87})};
    const std::array non_push_prevouts{non_push_prevout};
    const auto non_push_tx{tx(
        {bitcoin::tx_input{point(byte(1), 0), script({0x51}), 0xffffffffU}},
        {output()})};
    const auto non_push_script{bitcoin::verify_script(
        non_push_prevout.script(),
        non_push_prevout.value(),
        non_push_tx,
        0,
        bitcoin::verification_flags::none(),
        std::span<const bitcoin::tx_output>{non_push_prevouts})};
    if (auto failure{check(non_push_script.accepted(), __LINE__)}) return failure;

    const bitcoin::tx_output p2sh_true_prevout{bitcoin::amount{60}, script({
                                                                         0xa9, 0x14,
                                                                         0xda, 0x17, 0x45, 0xe9, 0xb5, 0x49, 0xbd, 0x0b, 0xfa, 0x1a,
                                                                         0x56, 0x99, 0x71, 0xc7, 0x7e, 0xba, 0x30, 0xcd, 0x5a, 0x4b,
                                                                         0x87,
                                                                     })};
    const std::array p2sh_true_prevouts{p2sh_true_prevout};
    const auto p2sh_true_tx{tx(
        {bitcoin::tx_input{point(byte(1), 0), script({0x01, 0x51}), 0xffffffffU}},
        {output()})};
    const auto p2sh_true_script{bitcoin::verify_script(
        p2sh_true_prevout.script(),
        p2sh_true_prevout.value(),
        p2sh_true_tx,
        0,
        bitcoin::verification_flags::p2sh(),
        std::span<const bitcoin::tx_output>{p2sh_true_prevouts})};
    if (auto failure{check(p2sh_true_script.accepted(), __LINE__)}) return failure;

    const bitcoin::tx_output p2sh_mismatch_prevout{bitcoin::amount{60}, script({
                                                                             0xa9, 0x14,
                                                                             0x00, 0x00, 0x00, 0x00, 0x00,
                                                                             0x00, 0x00, 0x00, 0x00, 0x00,
                                                                             0x00, 0x00, 0x00, 0x00, 0x00,
                                                                             0x00, 0x00, 0x00, 0x00, 0x00,
                                                                             0x87,
                                                                         })};
    const std::array p2sh_mismatch_prevouts{p2sh_mismatch_prevout};
    const auto p2sh_mismatch_script{bitcoin::verify_script(
        p2sh_mismatch_prevout.script(),
        p2sh_mismatch_prevout.value(),
        p2sh_true_tx,
        0,
        bitcoin::verification_flags::p2sh(),
        std::span<const bitcoin::tx_output>{p2sh_mismatch_prevouts})};
    if (auto failure{check(p2sh_mismatch_script.rejected(), __LINE__)}) return failure;

    const bitcoin::tx_output p2sh_false_prevout{bitcoin::amount{60}, script({
                                                                          0xa9, 0x14,
                                                                          0x9f, 0x7f, 0xd0, 0x96, 0xd3, 0x7e, 0xd2, 0xc0, 0xe3, 0xf7,
                                                                          0xf0, 0xcf, 0xc9, 0x24, 0xbe, 0xef, 0x4f, 0xfc, 0xeb, 0x68,
                                                                          0x87,
                                                                      })};
    const std::array p2sh_false_prevouts{p2sh_false_prevout};
    const auto p2sh_false_tx{tx(
        {bitcoin::tx_input{point(byte(1), 0), script({0x01, 0x00}), 0xffffffffU}},
        {output()})};
    const auto p2sh_false_script{bitcoin::verify_script(
        p2sh_false_prevout.script(),
        p2sh_false_prevout.value(),
        p2sh_false_tx,
        0,
        bitcoin::verification_flags::p2sh(),
        std::span<const bitcoin::tx_output>{p2sh_false_prevouts})};
    if (auto failure{check(p2sh_false_script.rejected(), __LINE__)}) return failure;

    const auto bad_input_index{bitcoin::verify_script(
        accepted_prevout.script(),
        accepted_prevout.value(),
        tx(),
        1,
        bitcoin::verification_flags::none(),
        std::span<const bitcoin::tx_output>{accepted_prevouts})};
    if (auto failure{check(bad_input_index.failed() && bad_input_index.assume_error().code() == bitcoin::operation_error_code::invariant_violation, __LINE__)}) return failure;

    const auto bad_prevout_count{bitcoin::verify_script(
        accepted_prevout.script(),
        accepted_prevout.value(),
        tx(),
        0,
        bitcoin::verification_flags::none(),
        std::span<const bitcoin::tx_output>{})};
    if (auto failure{check(bad_prevout_count.failed() && bad_prevout_count.assume_error().code() == bitcoin::operation_error_code::invariant_violation, __LINE__)}) return failure;

    const std::array witness_prevouts{bitcoin::tx_output{bitcoin::amount{60}, script({
                                                                                 0x00, 0x20,
                                                                                 0x4a, 0xe8, 0x15, 0x72, 0xf0, 0x6e, 0x1b, 0x88,
                                                                                 0xfd, 0x5c, 0xed, 0x7a, 0x1a, 0x00, 0x09, 0x45,
                                                                                 0x43, 0x2e, 0x83, 0xe1, 0x55, 0x1e, 0x6f, 0x72,
                                                                                 0x1e, 0xe9, 0xc0, 0x0b, 0x8c, 0xc3, 0x32, 0x60,
                                                                             })}};
    const auto witness_tx{tx(
        {bitcoin::tx_input{
            point(byte(1), 0),
            script({}),
            0xffffffffU,
            std::vector<bitcoin::witness_item>{witness_item({0x51})}}},
        {output()})};
    const auto accepted_witness{bitcoin::verify_script(
        witness_prevouts.front().script(),
        witness_prevouts.front().value(),
        witness_tx,
        0,
        bitcoin::verification_flags::witness(),
        std::span<const bitcoin::tx_output>{witness_prevouts})};
    if (auto failure{check(accepted_witness.accepted(), __LINE__)}) return failure;

    const std::array witness_mismatch_prevouts{bitcoin::tx_output{bitcoin::amount{60}, script({
                                                                                          0x00, 0x20,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                          0x00, 0x00, 0x00, 0x00,
                                                                                      })}};
    const auto rejected_witness_mismatch{bitcoin::verify_script(
        witness_mismatch_prevouts.front().script(),
        witness_mismatch_prevouts.front().value(),
        witness_tx,
        0,
        bitcoin::verification_flags::witness(),
        std::span<const bitcoin::tx_output>{witness_mismatch_prevouts})};
    if (auto failure{check(rejected_witness_mismatch.rejected(), __LINE__)}) return failure;

    const auto accepted{bitcoin::verify_transaction_scripts(
        tx(), spend_context(), script_context(), index)};
    if (auto failure{check(accepted.has_value() && accepted.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(accepted.assume_value().assume_facts().fee() == bitcoin::amount{10}, __LINE__)}) return failure;

    const auto intrinsic_accepted{bitcoin::verify_transaction_scripts(
        tx(), spend_context(), bitcoin::script_context{}, index)};
    if (auto failure{check(intrinsic_accepted.has_value() && intrinsic_accepted.assume_value().accepted(), __LINE__)}) return failure;

    in_memory_coin_index false_script_index;
    false_script_index.set(point(byte(1), 0), coin_with_script(60, script({0x00})));
    const auto intrinsic_rejected{bitcoin::verify_transaction_scripts(
        tx(), spend_context(), bitcoin::script_context{}, false_script_index)};
    if (auto failure{check(intrinsic_rejected.assume_value().assume_rejection().rule_id() == bitcoin::validation_rule_id::s07_scripts_validate, __LINE__)}) return failure;

    const auto two_input_tx{tx(
        {input(point(byte(1), 0)), input(point(byte(2), 0))},
        {output(90)})};
    const std::vector<bitcoin::coin> two_input_coins{coin(60), coin(40)};
    const auto two_input_result{bitcoin::verify_transaction_scripts(
        two_input_tx,
        spend_context(),
        script_context(),
        std::span<const bitcoin::coin>{two_input_coins})};
    if (auto failure{check(two_input_result.has_value() && two_input_result.assume_value().accepted(), __LINE__)}) return failure;

    in_memory_coin_index missing_index;
    const auto missing{bitcoin::verify_transaction_scripts(
        tx(), spend_context(), script_context(), missing_index)};
    if (auto failure{check(missing.assume_value().assume_rejection().rule_id() == bitcoin::validation_rule_id::s02_prevouts_unspent, __LINE__)}) return failure;

    const auto thrown_lookup{bitcoin::verify_transaction_scripts(
        tx(), spend_context(), script_context(), throwing_coin_index{})};
    if (auto failure{check(thrown_lookup.has_error() && thrown_lookup.assume_error().code() == bitcoin::operation_error_code::callback_failure, __LINE__)}) return failure;

    const auto coinbase{bitcoin::verify_transaction_scripts(
        coinbase_tx(), spend_context(), script_context(), missing_index)};
    if (auto failure{check(coinbase.has_value() && coinbase.assume_value().accepted(), __LINE__)}) return failure;

    const auto invariant{bitcoin::verify_transaction_scripts(
        tx(),
        spend_context(),
        script_context(),
        std::span<const bitcoin::coin>{})};
    if (auto failure{check(invariant.has_error() && invariant.assume_error().code() == bitcoin::operation_error_code::invariant_violation, __LINE__)}) return failure;

    return 0;
}
