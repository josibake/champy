// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/script.h>

#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <uint256.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace bitcoin {
namespace {

[[nodiscard]] script_execution_result failed(operation_error_code code, static_text reason) noexcept
{
    return script_execution_result::failed(operation_error::make(code, reason));
}

template <typename Hash>
[[nodiscard]] uint256 to_core_hash(Hash value) noexcept
{
    std::array<unsigned char, 32> bytes{};
    std::ranges::transform(as_bytes(value), bytes.begin(), [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
    return uint256{std::span<const unsigned char>{bytes}};
}

[[nodiscard]] std::uint32_t to_core_transaction_version(std::int32_t value) noexcept
{
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] CScript to_core_script(script_ref value)
{
    const auto bytes{as_bytes(value)};
    CScript result;
    result.reserve(bytes.size());
    std::ranges::transform(bytes, std::back_inserter(result), [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
    return result;
}

[[nodiscard]] std::vector<unsigned char> to_core_witness_item(const witness_item& value)
{
    const auto bytes{as_bytes(value)};
    std::vector<unsigned char> result;
    result.reserve(bytes.size());
    std::ranges::transform(bytes, std::back_inserter(result), [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
    return result;
}

[[nodiscard]] COutPoint to_core_outpoint(outpoint value) noexcept
{
    return COutPoint{Txid::FromUint256(to_core_hash(value.txid())), value.index().value()};
}

[[nodiscard]] CTxIn to_core_tx_input(const tx_input& value)
{
    CTxIn result{to_core_outpoint(value.previous_output()), to_core_script(value.script()), value.sequence()};
    result.scriptWitness.stack.reserve(value.witness().size());
    std::ranges::transform(value.witness(), std::back_inserter(result.scriptWitness.stack), [](const witness_item& item) {
        return to_core_witness_item(item);
    });
    return result;
}

[[nodiscard]] CTxOut to_core_tx_output(const tx_output& value)
{
    return CTxOut{value.value().satoshis(), to_core_script(value.script())};
}

[[nodiscard]] CTransaction to_core_transaction(const transaction& value)
{
    CMutableTransaction tx;
    tx.version = to_core_transaction_version(value.version());
    tx.nLockTime = value.locktime();
    tx.vin.reserve(value.inputs().size());
    std::ranges::transform(value.inputs(), std::back_inserter(tx.vin), [](const tx_input& input) {
        return to_core_tx_input(input);
    });
    tx.vout.reserve(value.outputs().size());
    std::ranges::transform(value.outputs(), std::back_inserter(tx.vout), [](const tx_output& output) {
        return to_core_tx_output(output);
    });
    return CTransaction{std::move(tx)};
}

[[nodiscard]] std::vector<CTxOut> to_core_spent_outputs(std::span<const tx_output> prevouts)
{
    std::vector<CTxOut> result;
    result.reserve(prevouts.size());
    std::ranges::transform(prevouts, std::back_inserter(result), [](const tx_output& output) {
        return to_core_tx_output(output);
    });
    return result;
}

[[nodiscard]] script_verify_flags to_core_flags(verification_flags flags) noexcept
{
    auto result{SCRIPT_VERIFY_NONE};
    if (flags.contains(verification_flags::p2sh())) {
        result |= SCRIPT_VERIFY_P2SH;
    }
    if (flags.contains(verification_flags::dersig())) {
        result |= SCRIPT_VERIFY_DERSIG;
    }
    if (flags.contains(verification_flags::nulldummy())) {
        result |= SCRIPT_VERIFY_NULLDUMMY;
    }
    if (flags.contains(verification_flags::checklocktimeverify())) {
        result |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }
    if (flags.contains(verification_flags::checksequenceverify())) {
        result |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }
    if (flags.contains(verification_flags::witness()) || flags.contains(verification_flags::taproot())) {
        result |= SCRIPT_VERIFY_P2SH;
        result |= SCRIPT_VERIFY_WITNESS;
    }
    if (flags.contains(verification_flags::taproot())) {
        result |= SCRIPT_VERIFY_TAPROOT;
    }
    return result;
}

[[nodiscard]] script_execution_result script_rejected(ScriptError) noexcept
{
    return script_execution_result::invalid("script interpreter rejected the input");
}

} // namespace

script_execution_result verify_script(
    script_ref previous_script,
    amount spent_amount,
    const transaction& spending_transaction,
    std::size_t input_index,
    verification_flags flags,
    std::span<const tx_output> prevouts)
{
    return [&]() -> script_execution_result {
        try {
            const auto inputs{spending_transaction.inputs()};
            if (input_index >= inputs.size()) {
                return failed(operation_error_code::invariant_violation, "script input index is outside the transaction input range");
            }
            if (prevouts.size() != inputs.size()) {
                return failed(operation_error_code::invariant_violation, "script prevout count does not match transaction input count");
            }
            if (prevouts[input_index].value() != spent_amount || prevouts[input_index].script() != previous_script) {
                return failed(operation_error_code::invariant_violation, "script prevout evidence does not match the input being verified");
            }
            if (input_index > std::numeric_limits<unsigned int>::max()) {
                return failed(operation_error_code::invariant_violation, "script input index exceeds the interpreter input range");
            }

            const auto core_tx{to_core_transaction(spending_transaction)};
            const auto core_input_index{static_cast<unsigned int>(input_index)};
            PrecomputedTransactionData txdata;
            txdata.Init(core_tx, to_core_spent_outputs(prevouts));

            ScriptError script_error{SCRIPT_ERR_OK};
            const auto previous_output{to_core_tx_output(prevouts[input_index])};
            const TransactionSignatureChecker checker{
                &core_tx,
                core_input_index,
                spent_amount.satoshis(),
                txdata,
                MissingDataBehavior::FAIL};

            if (VerifyScript(
                    core_tx.vin[core_input_index].scriptSig,
                    previous_output.scriptPubKey,
                    &core_tx.vin[core_input_index].scriptWitness,
                    to_core_flags(flags),
                    checker,
                    &script_error)) {
                return script_execution_result::valid();
            }

            return script_rejected(script_error);
        } catch (const std::bad_alloc&) {
            return failed(operation_error_code::resource_exhaustion, "script verification exhausted resources");
        } catch (...) {
            return failed(operation_error_code::internal_bug, "script verification threw an exception");
        }
    }();
}

namespace validation_support {

verify_result<spend_facts> invalid_script(static_text reason)
{
    return invalid_spend(validation_rejection::rule(
        validation_rejection_code::rule_violation,
        validation_rule_id::s07_scripts_validate,
        reason));
}

std::vector<tx_output> previous_outputs(std::span<const coin> input_coins)
{
    std::vector<tx_output> result;
    result.reserve(input_coins.size());
    for (const auto& input_coin : input_coins) {
        result.push_back(input_coin.output());
    }
    return result;
}

} // namespace validation_support

verify_result<spend_facts> verify_transaction_scripts(
    const transaction& tx,
    const spend_context& spend,
    script_context scripts,
    std::span<const coin> input_coins)
{
    return verify_exception_boundary<spend_facts>([&]() -> verify_result<spend_facts> {
        const transaction_context local_context{spend.limits};
        const auto local{assess_transaction_intrinsic(tx, local_context)};
        if (local.has_error()) {
            return verify_result<spend_facts>::failed(local.assume_error());
        }
        if (!local.assume_value().accepted()) {
            return validation_support::invalid_spend(local.assume_value().assume_rejection());
        }

        const auto& tx_facts{local.assume_value().assume_facts()};
        if (tx_facts.coinbase()) {
            return validation_support::verify_no_transaction_spends(tx_facts);
        }

        const auto spend_result{validation_support::assess_transaction_spends_with_coins(tx, tx_facts, spend, input_coins)};
        if (spend_result.has_error() || !spend_result.assume_value().accepted()) {
            return spend_result;
        }

        const auto prevouts{validation_support::previous_outputs(input_coins)};
        for (std::size_t input_index{0}; input_index < tx.inputs().size(); ++input_index) {
            const auto& previous_output{prevouts[input_index]};
            const auto result{verify_script(
                previous_output.script(),
                previous_output.value(),
                tx,
                input_index,
                scripts.flags,
                prevouts)};
            if (result.accepted()) {
                continue;
            }
            if (result.rejected()) {
                return validation_support::invalid_script(result.assume_rejection());
            }
            return verify_result<spend_facts>::failed(result.assume_error());
        }

        return spend_result;
    },
                                                  "transaction script validation exhausted resources", operation_error_code::internal_bug, "transaction script validation threw an exception");
}

} // namespace bitcoin
