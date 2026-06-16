// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/spend.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace bitcoin {
namespace {

[[nodiscard]] validation_rejection reject(validation_rule_id id, static_text reason) noexcept
{
    return validation_rejection::rule(validation_rejection_code::rule_violation, id, reason);
}

[[nodiscard]] verify_result<spend_facts> invalid(validation_rule_id id, static_text reason)
{
    return validation_support::invalid_spend(reject(id, reason));
}

[[nodiscard]] verify_result<spend_facts> failed(operation_error_code code, static_text reason)
{
    return verify_result<spend_facts>::failed(operation_error::make(code, reason));
}

[[nodiscard]] bool money_range(std::int64_t value, amount max_money) noexcept
{
    return value >= 0 && value <= max_money.satoshis();
}

[[nodiscard]] std::int64_t seconds_since_epoch(std::chrono::sys_seconds value) noexcept
{
    return value.time_since_epoch().count();
}

[[nodiscard]] bool sequence_locks_satisfied(
    const transaction& tx,
    const spend_context& context,
    std::span<const coin> input_coins) noexcept
{
    static constexpr std::uint32_t disable_flag{1U << 31};
    static constexpr std::uint32_t type_flag{1U << 22};
    static constexpr std::uint32_t locktime_mask{0x0000ffffU};
    static constexpr int granularity{9};

    if (!context.locktime.verify_sequence() || tx.version() < 2) {
        return true;
    }

    std::int64_t minimum_height{-1};
    std::int64_t minimum_time{-1};
    for (std::size_t input_index{0}; input_index < tx.inputs().size(); ++input_index) {
        const auto sequence{tx.inputs()[input_index].sequence()};
        if ((sequence & disable_flag) != 0) {
            continue;
        }

        const auto relative_lock{static_cast<std::int64_t>(sequence & locktime_mask)};
        const auto& input_coin{input_coins[input_index]};
        if ((sequence & type_flag) != 0) {
            const auto previous_time{seconds_since_epoch(input_coin.previous_median_time_past().value())};
            minimum_time = std::max(
                minimum_time,
                previous_time + (relative_lock << granularity) - 1);
        } else {
            minimum_height = std::max(
                minimum_height,
                static_cast<std::int64_t>(input_coin.height().value()) + relative_lock - 1);
        }
    }

    return minimum_height < static_cast<std::int64_t>(context.height.value()) &&
           minimum_time < seconds_since_epoch(context.previous_median_time_past.value());
}

} // namespace

namespace validation_support {

verify_result<spend_facts> invalid_spend(validation_rejection rejection)
{
    return verify_result<spend_facts>::ok(validation_decision<spend_facts>::invalid(rejection));
}

operation_error coin_lookup_error(coin_lookup_failure failure)
{
    switch (failure) {
    case coin_lookup_failure::unavailable:
        return operation_error::make(operation_error_code::data_unavailable, "coin lookup data is unavailable");
    case coin_lookup_failure::malformed_stored_data:
        return operation_error::make(operation_error_code::malformed_stored_data, "stored coin data is malformed");
    case coin_lookup_failure::interrupted:
        return operation_error::make(operation_error_code::interruption, "coin lookup interrupted");
    case coin_lookup_failure::io_failure:
        return operation_error::make(operation_error_code::io_read, "coin lookup read failed");
    }
    return operation_error::make(operation_error_code::internal_bug, "unknown coin lookup failure");
}

verify_result<spend_facts> failed_coin_lookup(coin_lookup_failure failure)
{
    return verify_result<spend_facts>::failed(coin_lookup_error(failure));
}

verify_result<spend_facts> verify_no_transaction_spends(
    const transaction_facts& tx_facts)
{
    return verify_result<spend_facts>::ok(validation_decision<spend_facts>::valid(
        spend_facts{amount{0}, tx_facts.output_value(), amount{0}}));
}

verify_result<spend_facts> assess_transaction_spends_with_coins(
    const transaction& tx,
    const transaction_facts& tx_facts,
    const spend_context& context,
    std::span<const coin> input_coins)
{
    if (input_coins.size() != tx.inputs().size()) {
        return failed(operation_error_code::invariant_violation, "input coin count does not match transaction inputs");
    }

    const auto max_money{context.limits.max_money};
    std::int64_t input_sum{0};
    for (const auto& input_coin : input_coins) {
        if (input_coin.coinbase()) {
            const auto depth{
                static_cast<std::int64_t>(context.height.value()) -
                static_cast<std::int64_t>(input_coin.height().value())};
            if (depth < context.coinbase_maturity) {
                return invalid(validation_rule_id::s09_coinbase_maturity, "coinbase input is not mature");
            }
        }

        const auto value{input_coin.output().value().satoshis()};
        if (!money_range(value, max_money)) {
            return invalid(validation_rule_id::s06_input_value_and_fee_range, "transaction input value is outside money range");
        }
        if (input_sum > max_money.satoshis() - value) {
            return invalid(validation_rule_id::s06_input_value_and_fee_range, "transaction input sum exceeds maximum money");
        }
        input_sum += value;
    }

    const auto output_value{tx_facts.output_value().satoshis()};
    if (input_sum < output_value) {
        return invalid(validation_rule_id::s05_outputs_do_not_exceed_inputs, "transaction outputs exceed inputs");
    }

    const auto fee{input_sum - output_value};
    if (!money_range(fee, max_money)) {
        return invalid(validation_rule_id::s06_input_value_and_fee_range, "transaction fee is outside money range");
    }

    if (!sequence_locks_satisfied(tx, context, input_coins)) {
        return invalid(validation_rule_id::s08_sequence_locks, "transaction sequence locks are not satisfied");
    }

    return verify_result<spend_facts>::ok(validation_decision<spend_facts>::valid(
        spend_facts{amount{input_sum}, tx_facts.output_value(), amount{fee}}));
}

} // namespace validation_support

} // namespace bitcoin
