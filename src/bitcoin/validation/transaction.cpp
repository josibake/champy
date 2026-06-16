// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/transaction.h>

#include <bitcoin/protocol/codec.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace bitcoin {
namespace {

[[nodiscard]] validation_rejection reject(validation_rule_id id, static_text reason) noexcept
{
    return validation_rejection::rule(validation_rejection_code::rule_violation, id, reason);
}

[[nodiscard]] verify_result<transaction_facts> invalid(validation_rule_id id, static_text reason)
{
    return verify_result<transaction_facts>::ok(validation_decision<transaction_facts>::invalid(reject(id, reason)));
}

[[nodiscard]] bool is_coinbase(const transaction& tx) noexcept
{
    return tx.inputs().size() == 1 && tx.inputs().front().previous_output().is_null();
}

[[nodiscard]] bool has_duplicate_inputs(const transaction& tx)
{
    std::vector<outpoint> previous_outputs;
    previous_outputs.reserve(tx.inputs().size());
    for (const auto& input : tx.inputs()) {
        previous_outputs.push_back(input.previous_output());
    }
    std::ranges::sort(previous_outputs);
    return std::ranges::adjacent_find(previous_outputs) != previous_outputs.end();
}

[[nodiscard]] bool coinbase_script_size_valid(const transaction& tx) noexcept
{
    const auto size{as_bytes(tx.inputs().front().script()).size()};
    return size >= 2 && size <= 100;
}

[[nodiscard]] bool stripped_weight_exceeds_limit(std::size_t stripped_size, std::size_t limit) noexcept
{
    return stripped_size > limit / witness_scale_factor;
}

[[nodiscard]] std::size_t stripped_weight(std::size_t stripped_size) noexcept
{
    return stripped_size * witness_scale_factor;
}

[[nodiscard]] bool final_under_locktime(
    const transaction& tx,
    const transaction_finality_context& context) noexcept
{
    static constexpr std::uint32_t locktime_threshold{500'000'000U};
    static constexpr std::uint32_t sequence_final{0xffffffffU};

    if (tx.locktime() == 0) {
        return true;
    }

    const auto locktime_comparison{
        tx.locktime() < locktime_threshold ?
            static_cast<std::int64_t>(context.height.value()) :
            static_cast<std::int64_t>(context.timestamp.seconds_since_epoch())};
    if (static_cast<std::int64_t>(tx.locktime()) < locktime_comparison) {
        return true;
    }

    return std::ranges::all_of(tx.inputs(), [](const tx_input& input) {
        return input.sequence() == sequence_final;
    });
}

} // namespace

verify_result<transaction_facts> assess_transaction_intrinsic(
    const transaction& tx,
    const transaction_context& context)
{
    return verify_exception_boundary<transaction_facts>([&]() -> verify_result<transaction_facts> {
        if (tx.inputs().empty()) {
            return invalid(validation_rule_id::l07_transaction_inputs_non_empty, "transaction has no inputs");
        }
        if (tx.outputs().empty()) {
            return invalid(validation_rule_id::l08_transaction_outputs_non_empty, "transaction has no outputs");
        }

        const auto serialized_without_witness{stripped_serialized_size(tx)};
        if (stripped_weight_exceeds_limit(serialized_without_witness, context.limits.max_stripped_weight)) {
            return invalid(validation_rule_id::l09_transaction_size, "transaction stripped size exceeds maximum weight");
        }

        std::int64_t output_sum{0};
        for (const auto& output : tx.outputs()) {
            const auto value{output.value().satoshis()};
            if (value < 0) {
                return invalid(validation_rule_id::l10_output_value_non_negative, "transaction output value is negative");
            }
            if (value > context.limits.max_money.satoshis()) {
                return invalid(validation_rule_id::l11_output_value_range, "transaction output value exceeds maximum money");
            }
            if (output_sum > context.limits.max_money.satoshis() - value) {
                return invalid(validation_rule_id::l11_output_value_range, "transaction output sum exceeds maximum money");
            }
            output_sum += value;
        }

        if (has_duplicate_inputs(tx)) {
            return invalid(validation_rule_id::l12_unique_inputs, "transaction contains duplicate inputs");
        }

        if (is_coinbase(tx)) {
            if (!coinbase_script_size_valid(tx)) {
                return invalid(validation_rule_id::l13_coinbase_script_size, "coinbase script size is outside consensus range");
            }
        } else {
            for (const auto& input : tx.inputs()) {
                if (input.previous_output().is_null()) {
                    return invalid(validation_rule_id::l14_non_coinbase_prevout, "non-coinbase transaction spends null prevout");
                }
            }
        }

        return verify_result<transaction_facts>::ok(validation_decision<transaction_facts>::valid(
            transaction_facts{
                tx.id(),
                tx.witness_id(),
                amount{output_sum},
                is_coinbase(tx) ? transaction_kind::coinbase : transaction_kind::ordinary,
                stripped_weight(serialized_without_witness)}));
    },
                                                        "transaction validation exhausted resources", operation_error_code::internal_bug, "transaction validation threw an exception");
}

verify_result<transaction_facts> assess_transaction_finality(
    const transaction& tx,
    const transaction_finality_context& context)
{
    return verify_exception_boundary<transaction_facts>([&]() -> verify_result<transaction_facts> {
        transaction_context local_context;
        local_context.limits = context.limits;

        const auto local{assess_transaction_intrinsic(tx, local_context)};
        if (local.has_error() || !local.assume_value().accepted()) {
            return local;
        }

        if (!final_under_locktime(tx, context)) {
            return invalid(validation_rule_id::c01_transaction_finality, "transaction locktime is not final");
        }

        return local;
    },
                                                        "transaction finality validation exhausted resources", operation_error_code::internal_bug, "transaction finality validation threw an exception");
}

} // namespace bitcoin
