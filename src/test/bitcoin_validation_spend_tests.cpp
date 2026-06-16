// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <new>
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
    std::int32_t version = 1)
{
    return bitcoin::transaction{version, std::move(inputs), std::move(outputs), 0};
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

bitcoin::coin coin(
    std::int64_t value,
    std::int32_t height = 100,
    bool coinbase = false,
    bitcoin::median_time_past previous_mtp = mtp(1231006505))
{
    return bitcoin::coin{output(value), bitcoin::block_height{height}, coinbase, previous_mtp};
}

bitcoin::spend_context context() noexcept
{
    bitcoin::spend_context result;
    result.height = bitcoin::block_height{200};
    result.previous_median_time_past = mtp(1231007000);
    result.limits.max_money = bitcoin::amount{100};
    result.coinbase_maturity = 100;
    return result;
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

class fallible_coin_source
{
public:
    void set(bitcoin::outpoint point, bitcoin::coin_lookup_result result)
    {
        m_entries.push_back(entry{point, std::move(result)});
    }

    [[nodiscard]] bitcoin::coin_lookup_result lookup(const bitcoin::outpoint& point) const
    {
        const auto match{std::ranges::find_if(m_entries, [&](const entry& candidate) {
            return candidate.point == point;
        })};
        if (match == m_entries.end()) {
            return bitcoin::coin_lookup_result::missing();
        }
        return match->result;
    }

private:
    struct entry {
        bitcoin::outpoint point;
        bitcoin::coin_lookup_result result;
    };

    std::vector<entry> m_entries;
};

static_assert(!bitcoin::coin_index<fallible_coin_source>);
static_assert(bitcoin::fallible_coin_source<fallible_coin_source>);

class throwing_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint&) const
    {
        throw std::runtime_error{"coin lookup failed"};
    }
};

class exhausted_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint&) const
    {
        throw std::bad_alloc{};
    }
};

static_assert(bitcoin::coin_index<throwing_coin_index>);
static_assert(bitcoin::coin_index<exhausted_coin_index>);

const bitcoin::validation_rejection& rejection(const bitcoin::verify_result<bitcoin::spend_facts>& result)
{
    return result.assume_value().assume_rejection();
}

template <typename Coins>
bitcoin::validation_rule_id rejected_by(const bitcoin::transaction& candidate, const Coins& index)
{
    const auto result{bitcoin::verify(candidate, context(), index)};
    return rejection(result).rule_id();
}

template <typename Coins>
bitcoin::operation_error_code failed_by(const bitcoin::transaction& candidate, const Coins& index)
{
    const auto result{bitcoin::verify(candidate, context(), index)};
    return result.assume_error().code();
}

} // namespace

int main()
{
    in_memory_coin_index index;
    index.set(point(byte(1), 0), coin(60));

    const auto accepted{bitcoin::verify(tx(), context(), index)};
    if (auto failure{check(accepted.has_value() && accepted.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(accepted.assume_value().assume_facts().input_value() == bitcoin::amount{60}, __LINE__)}) return failure;
    if (auto failure{check(accepted.assume_value().assume_facts().output_value() == bitcoin::amount{50}, __LINE__)}) return failure;
    if (auto failure{check(accepted.assume_value().assume_facts().fee() == bitcoin::amount{10}, __LINE__)}) return failure;

    in_memory_coin_index missing_index;
    if (auto failure{check(rejected_by(tx(), missing_index) == bitcoin::validation_rule_id::s02_prevouts_unspent, __LINE__)}) return failure;

    fallible_coin_source spent_index;
    spent_index.set(point(byte(1), 0), bitcoin::coin_lookup_result::spent());
    if (auto failure{check(rejected_by(tx(), spent_index) == bitcoin::validation_rule_id::s02_prevouts_unspent, __LINE__)}) return failure;

    fallible_coin_source unavailable_index;
    unavailable_index.set(point(byte(1), 0), bitcoin::coin_lookup_result::unavailable("snapshot not ready"));
    if (auto failure{check(failed_by(tx(), unavailable_index) == bitcoin::operation_error_code::data_unavailable, __LINE__)}) return failure;

    fallible_coin_source malformed_index;
    malformed_index.set(point(byte(1), 0), bitcoin::coin_lookup_result::malformed_stored_data("bad coin"));
    if (auto failure{check(failed_by(tx(), malformed_index) == bitcoin::operation_error_code::malformed_stored_data, __LINE__)}) return failure;

    fallible_coin_source interrupted_index;
    interrupted_index.set(point(byte(1), 0), bitcoin::coin_lookup_result::interrupted("stop requested"));
    if (auto failure{check(failed_by(tx(), interrupted_index) == bitcoin::operation_error_code::interruption, __LINE__)}) return failure;

    fallible_coin_source io_index;
    io_index.set(point(byte(1), 0), bitcoin::coin_lookup_result::io_failure("read failed"));
    if (auto failure{check(failed_by(tx(), io_index) == bitcoin::operation_error_code::io_read, __LINE__)}) return failure;

    const auto thrown_lookup{bitcoin::verify(tx(), context(), throwing_coin_index{})};
    if (auto failure{check(thrown_lookup.has_error() && thrown_lookup.assume_error().code() == bitcoin::operation_error_code::callback_failure, __LINE__)}) return failure;

    const auto exhausted_lookup{bitcoin::verify(tx(), context(), exhausted_coin_index{})};
    if (auto failure{check(exhausted_lookup.has_error() && exhausted_lookup.assume_error().code() == bitcoin::operation_error_code::resource_exhaustion, __LINE__)}) return failure;

    in_memory_coin_index low_input_index;
    low_input_index.set(point(byte(1), 0), coin(49));
    if (auto failure{check(rejected_by(tx(), low_input_index) == bitcoin::validation_rule_id::s05_outputs_do_not_exceed_inputs, __LINE__)}) return failure;

    in_memory_coin_index bad_input_index;
    bad_input_index.set(point(byte(1), 0), coin(-1));
    if (auto failure{check(rejected_by(tx({input()}, {output(0)}), bad_input_index) == bitcoin::validation_rule_id::s06_input_value_and_fee_range, __LINE__)}) return failure;

    in_memory_coin_index input_sum_index;
    input_sum_index.set(point(byte(2), 0), coin(60));
    input_sum_index.set(point(byte(3), 0), coin(41));
    if (auto failure{check(
            rejected_by(tx({input(point(byte(2), 0)), input(point(byte(3), 0))}, {output()}), input_sum_index) ==
                bitcoin::validation_rule_id::s06_input_value_and_fee_range,
            __LINE__)}) return failure;

    in_memory_coin_index immature_coinbase_index;
    immature_coinbase_index.set(point(byte(1), 0), coin(60, 101, true));
    if (auto failure{check(rejected_by(tx(), immature_coinbase_index) == bitcoin::validation_rule_id::s09_coinbase_maturity, __LINE__)}) return failure;

    in_memory_coin_index mature_coinbase_index;
    mature_coinbase_index.set(point(byte(1), 0), coin(60, 100, true));
    if (auto failure{check(bitcoin::verify(tx(), context(), mature_coinbase_index).assume_value().accepted(), __LINE__)}) return failure;

    auto lock_context{context()};
    lock_context.locktime = bitcoin::locktime_flags::sequence();
    lock_context.height = bitcoin::block_height{14};
    in_memory_coin_index sequence_index;
    sequence_index.set(point(byte(4), 0), coin(60, 10, false));
    if (auto failure{check(
            bitcoin::verify(tx({input(point(byte(4), 0), 5)}, {output()}, 2), lock_context, sequence_index)
                    .assume_value()
                    .assume_rejection()
                    .rule_id() == bitcoin::validation_rule_id::s08_sequence_locks,
            __LINE__)}) return failure;

    lock_context.height = bitcoin::block_height{15};
    if (auto failure{check(
            bitcoin::verify(tx({input(point(byte(4), 0), 5)}, {output()}, 2), lock_context, sequence_index)
                .assume_value()
                .accepted(),
            __LINE__)}) return failure;

    if (auto failure{check(
            bitcoin::verify(coinbase_tx(), context(), missing_index).assume_value().accepted(),
            __LINE__)}) return failure;

    return 0;
}
