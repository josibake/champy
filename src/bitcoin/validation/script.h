// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_SCRIPT_H
#define BITCOIN_BITCOIN_VALIDATION_SCRIPT_H

#include <bitcoin/protocol/coin_index.h>
#include <bitcoin/protocol/script.h>
#include <bitcoin/protocol/transaction.h>
#include <bitcoin/validation/context.h>
#include <bitcoin/validation/result.h>
#include <bitcoin/validation/spend.h>
#include <bitcoin/validation/transaction.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace bitcoin {

struct script_context {
    verification_flags flags;
};

class script_execution_result
{
public:
    [[nodiscard]] static script_execution_result valid() noexcept
    {
        return script_execution_result{valid_tag{}};
    }

    [[nodiscard]] static script_execution_result invalid(static_text reason) noexcept
    {
        return script_execution_result{reason};
    }

    [[nodiscard]] static script_execution_result failed(operation_error error) noexcept
    {
        return script_execution_result{error};
    }

    [[nodiscard]] bool accepted() const noexcept { return std::holds_alternative<valid_tag>(m_value); }
    [[nodiscard]] bool rejected() const noexcept { return std::holds_alternative<static_text>(m_value); }
    [[nodiscard]] bool failed() const noexcept { return std::holds_alternative<operation_error>(m_value); }
    [[nodiscard]] static_text assume_rejection() const { return std::get<static_text>(m_value); }
    [[nodiscard]] std::string_view assume_rejection_reason() const { return assume_rejection().view(); }
    [[nodiscard]] const operation_error& assume_error() const& { return std::get<operation_error>(m_value); }

private:
    struct valid_tag {
        friend constexpr bool operator==(valid_tag, valid_tag) noexcept = default;
    };

    explicit script_execution_result(valid_tag value) noexcept : m_value{value} {}
    explicit script_execution_result(static_text reason) noexcept : m_value{reason} {}
    explicit script_execution_result(operation_error error) noexcept : m_value{error} {}

    std::variant<valid_tag, static_text, operation_error> m_value;
};

[[nodiscard]] script_execution_result verify_script(
    script_ref previous_script,
    amount spent_amount,
    const transaction& spending_transaction,
    std::size_t input_index,
    verification_flags flags,
    std::span<const tx_output> prevouts);

namespace validation_support {

// Support functions kept visible only because public templates need their
// contracts. The release-facing operation names are the free verify(...)
// overloads in this namespace's parent.

[[nodiscard]] verify_result<spend_facts> invalid_script(static_text reason);

[[nodiscard]] std::vector<tx_output> previous_outputs(std::span<const coin> input_coins);

} // namespace validation_support

[[nodiscard]] verify_result<spend_facts> verify_transaction_scripts(
    const transaction& tx,
    const spend_context& spend,
    script_context scripts,
    std::span<const coin> input_coins);

[[nodiscard]] inline verify_result<spend_facts> verify(
    const transaction& tx,
    const spend_context& spend,
    script_context scripts,
    std::span<const coin> input_coins)
{
    return verify_transaction_scripts(tx, spend, scripts, input_coins);
}

template <validation_coin_source Coins>
[[nodiscard]] verify_result<spend_facts> verify_transaction_scripts(
    const transaction& tx,
    const spend_context& spend,
    script_context scripts,
    const Coins& index)
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

        const auto input_lookup{validation_support::lookup_input_coins(tx, index)};
        if (input_lookup.has_error()) {
            return verify_result<spend_facts>::failed(input_lookup.assume_error());
        }
        if (!input_lookup.assume_value().accepted()) {
            return validation_support::invalid_spend(input_lookup.assume_value().assume_rejection());
        }

        const auto& input_coins{input_lookup.assume_value().assume_facts()};
        return verify_transaction_scripts(tx, spend, scripts, input_coins);
    },
                                                  "transaction script validation exhausted resources", operation_error_code::internal_bug, "transaction script validation threw an exception");
}

template <validation_coin_source Coins>
[[nodiscard]] verify_result<spend_facts> verify(
    const transaction& tx,
    const spend_context& spend,
    script_context scripts,
    const Coins& index)
{
    return verify_transaction_scripts(tx, spend, scripts, index);
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_SCRIPT_H
