// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_SPEND_H
#define BITCOIN_BITCOIN_VALIDATION_SPEND_H

#include <bitcoin/protocol/coin_index.h>
#include <bitcoin/protocol/transaction.h>
#include <bitcoin/validation/context.h>
#include <bitcoin/validation/result.h>
#include <bitcoin/validation/transaction.h>

#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace bitcoin {

class spend_facts;

namespace validation_support {

// Support functions kept visible only because public templates need their
// contracts. The release-facing operation names are the free verify(...)
// overloads in this namespace's parent.

[[nodiscard]] verify_result<spend_facts> invalid_spend(validation_rejection rejection);
[[nodiscard]] operation_error coin_lookup_error(coin_lookup_failure failure);
[[nodiscard]] verify_result<spend_facts> failed_coin_lookup(coin_lookup_failure failure);
[[nodiscard]] verify_result<spend_facts> verify_no_transaction_spends(
    const transaction_facts& tx_facts);
[[nodiscard]] verify_result<spend_facts> assess_transaction_spends_with_coins(
    const transaction& tx,
    const transaction_facts& tx_facts,
    const spend_context& context,
    std::span<const coin> input_coins);

} // namespace validation_support

class spend_facts
{
public:
    [[nodiscard]] amount input_value() const noexcept { return m_input_value; }
    [[nodiscard]] amount output_value() const noexcept { return m_output_value; }
    [[nodiscard]] amount fee() const noexcept { return m_fee; }

    friend constexpr bool operator==(const spend_facts&, const spend_facts&) noexcept = default;

private:
    friend verify_result<spend_facts> validation_support::assess_transaction_spends_with_coins(
        const transaction& tx,
        const transaction_facts& tx_facts,
        const spend_context& context,
        std::span<const coin> input_coins);

    friend verify_result<spend_facts> validation_support::verify_no_transaction_spends(
        const transaction_facts& tx_facts);

    constexpr spend_facts(amount input_value, amount output_value, amount fee) noexcept : m_input_value{input_value},
                                                                                          m_output_value{output_value},
                                                                                          m_fee{fee}
    {
    }

    amount m_input_value;
    amount m_output_value;
    amount m_fee;
};

template <typename Coins>
concept validation_coin_source =
    coin_index<Coins> || fallible_coin_source<Coins>;

namespace validation_support {

using input_coin_lookup_result = operation_result<validation_decision<std::vector<coin>>>;
using coin_lookup_operation_result = operation_result<std::optional<coin>>;

template <coin_index Coins>
[[nodiscard]] coin_lookup_operation_result lookup_coin(
    const Coins& index,
    const outpoint& point)
{
    try {
        return coin_lookup_operation_result::ok(std::optional<coin>{index(point)});
    } catch (const std::bad_alloc&) {
        return coin_lookup_operation_result::failed(operation_error::make(
            operation_error_code::resource_exhaustion,
            "coin index lookup exhausted resources"));
    } catch (...) {
        return coin_lookup_operation_result::failed(operation_error::make(
            operation_error_code::callback_failure,
            "coin index lookup threw an exception"));
    }
}

template <fallible_coin_source Coins>
    requires (!coin_index<Coins>)
[[nodiscard]] coin_lookup_operation_result lookup_coin(
    const Coins& index,
    const outpoint& point)
{
    try {
        auto lookup{index.lookup(point)};
        switch (lookup.state()) {
        case coin_lookup_state::found:
            return coin_lookup_operation_result::ok(
                std::optional<coin>{std::move(lookup).assume_value()});
        case coin_lookup_state::missing:
        case coin_lookup_state::spent:
            return coin_lookup_operation_result::ok(std::nullopt);
        case coin_lookup_state::unavailable:
        case coin_lookup_state::malformed_stored_data:
        case coin_lookup_state::interrupted:
        case coin_lookup_state::io_failure:
            return coin_lookup_operation_result::failed(coin_lookup_error(lookup.assume_failure()));
        }
        return coin_lookup_operation_result::failed(operation_error::make(
            operation_error_code::internal_bug,
            "unknown coin lookup state"));
    } catch (const std::bad_alloc&) {
        return coin_lookup_operation_result::failed(operation_error::make(
            operation_error_code::resource_exhaustion,
            "coin index lookup exhausted resources"));
    } catch (...) {
        return coin_lookup_operation_result::failed(operation_error::make(
            operation_error_code::callback_failure,
            "coin index lookup threw an exception"));
    }
}

template <validation_coin_source Coins>
[[nodiscard]] input_coin_lookup_result lookup_input_coins(
    const transaction& tx,
    const Coins& index)
{
    return operation_exception_boundary<validation_decision<std::vector<coin>>>([&]() -> input_coin_lookup_result {
        std::vector<coin> input_coins;
        input_coins.reserve(tx.inputs().size());
        for (const auto& input : tx.inputs()) {
            auto lookup_result{lookup_coin(index, input.previous_output())};
            if (lookup_result.has_error()) {
                return input_coin_lookup_result::failed(lookup_result.assume_error());
            }

            auto lookup{std::move(lookup_result).assume_value()};
            if (!lookup) {
                return input_coin_lookup_result::ok(validation_decision<std::vector<coin>>::invalid(
                    validation_rejection::rule(
                        validation_rejection_code::rule_violation,
                        validation_rule_id::s02_prevouts_unspent,
                        "transaction input prevout is not present")));
            }
            input_coins.push_back(std::move(*lookup));
        }

        return input_coin_lookup_result::ok(validation_decision<std::vector<coin>>::valid(std::move(input_coins)));
    },
                                                                                "input coin lookup exhausted resources", operation_error_code::internal_bug, "input coin lookup threw an exception");
}

} // namespace validation_support

// Expert building block for spend checks when the caller intentionally
// validates transaction phases separately. Prefer verify(tx, context, coins) for
// normal consumers.
template <validation_coin_source Coins>
[[nodiscard]] verify_result<spend_facts> assess_transaction_spends(
    const transaction& tx,
    const spend_context& context,
    const Coins& index)
{
    return verify_exception_boundary<spend_facts>([&]() -> verify_result<spend_facts> {
        const transaction_context local_context{context.limits};
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
        return validation_support::assess_transaction_spends_with_coins(tx, tx_facts, context, input_coins);
    },
                                                  "transaction spend validation exhausted resources", operation_error_code::internal_bug, "transaction spend validation threw an exception");
}

template <validation_coin_source Coins>
[[nodiscard]] verify_result<spend_facts> verify(
    const transaction& tx,
    const spend_context& context,
    const Coins& index)
{
    return assess_transaction_spends(tx, context, index);
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_SPEND_H
