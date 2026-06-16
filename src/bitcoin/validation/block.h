// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_BLOCK_H
#define BITCOIN_BITCOIN_VALIDATION_BLOCK_H

#include <bitcoin/protocol/block.h>
#include <bitcoin/protocol/coin_index.h>
#include <bitcoin/protocol/hash.h>
#include <bitcoin/validation/context.h>
#include <bitcoin/validation/result.h>
#include <bitcoin/validation/script.h>
#include <bitcoin/validation/spend.h>
#include <bitcoin/validation/transaction.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace bitcoin {

class block_facts;

struct block_limits {
    transaction_context transactions;
    std::size_t max_block_weight{4'000'000};
    std::size_t max_legacy_sigop_cost{80'000};
};

struct block_local_context {
    block_limits limits;
    bool check_merkle_root{true};
};

struct block_deployments {
    bool segwit_active{false};
    bool height_in_coinbase_active{false};
};

struct block_contextual_context {
    block_local_context local;
    block_height height;
    block_time locktime_cutoff;
    block_deployments deployments;
};

struct deployment_state {
    bool enforce_bip30{false};
};

struct block_spend_context {
    block_local_context local;
    spend_context spend;
    script_context scripts;
    deployment_state deployments;
    amount subsidy;
    std::size_t max_sigop_cost{80'000};
};

struct spent_output {
    outpoint point;
    coin previous_coin;
};

struct created_output {
    outpoint point;
    tx_output output;
    block_height height;
    bool coinbase{false};
    median_time_past previous_median_time_past;
};

namespace validation_support {

// Callback bridge used by the public assess_block_spends(...) templates. Each
// callback borrows its state only for the duration of the call and reports
// operational failure through operation_result, not exceptions.
using coin_lookup_callback = coin_lookup_operation_result (*)(
    const void* state,
    const outpoint& point);

[[nodiscard]] verify_result<block_facts> assess_block_spends_with_callbacks(
    const block& candidate,
    const block_spend_context& context,
    const void* coins,
    coin_lookup_callback lookup_coin);

} // namespace validation_support

class witness_commitment
{
public:
    [[nodiscard]] static constexpr witness_commitment none() noexcept
    {
        return witness_commitment{};
    }

    [[nodiscard]] static constexpr witness_commitment found(tx_output_index output_index, hash256 hash) noexcept
    {
        return witness_commitment{true, output_index, hash};
    }

    [[nodiscard]] constexpr bool present() const noexcept { return m_present; }
    [[nodiscard]] constexpr tx_output_index output_index() const noexcept { return m_output_index; }
    [[nodiscard]] constexpr hash256 hash() const noexcept { return m_hash; }

    friend constexpr bool operator==(const witness_commitment&, const witness_commitment&) noexcept = default;

private:
    constexpr witness_commitment() noexcept = default;
    constexpr witness_commitment(bool present, tx_output_index output_index, hash256 hash) noexcept : m_present{present},
                                                                                                      m_output_index{output_index},
                                                                                                      m_hash{hash}
    {
    }

    bool m_present{false};
    tx_output_index m_output_index;
    hash256 m_hash;
};

class block_facts
{
public:
    [[nodiscard]] block_hash hash() const noexcept { return m_hash; }
    [[nodiscard]] hash256 merkle_root() const noexcept { return m_merkle_root; }
    [[nodiscard]] std::size_t transaction_count() const noexcept { return m_transaction_count; }
    [[nodiscard]] std::size_t serialized_size() const noexcept { return m_serialized_size; }
    [[nodiscard]] std::size_t stripped_size() const noexcept { return m_stripped_size; }
    [[nodiscard]] std::size_t weight() const noexcept { return m_weight; }
    [[nodiscard]] std::size_t legacy_sigop_cost() const noexcept { return m_legacy_sigop_cost; }
    [[nodiscard]] std::size_t sigop_cost() const noexcept { return m_sigop_cost; }
    [[nodiscard]] hash256 witness_merkle_root() const noexcept { return m_witness_merkle_root; }
    [[nodiscard]] bool has_witness() const noexcept { return m_has_witness; }
    [[nodiscard]] witness_commitment commitment() const noexcept { return m_commitment; }
    [[nodiscard]] std::span<const transaction_facts> transactions() const noexcept { return m_transactions; }
    [[nodiscard]] std::span<const spend_facts> spends() const noexcept { return m_spends; }
    [[nodiscard]] std::span<const spent_output> spent_outputs() const noexcept { return m_spent_outputs; }
    [[nodiscard]] std::span<const created_output> created_outputs() const noexcept { return m_created_outputs; }
    [[nodiscard]] amount total_fees() const noexcept { return m_total_fees; }
    [[nodiscard]] amount coinbase_output_value() const noexcept { return m_coinbase_output_value; }

    friend constexpr bool operator==(const block_facts&, const block_facts&) noexcept = default;

private:
    friend verify_result<block_facts> assess_block_intrinsic(
        const block& candidate,
        const block_local_context& context);

    friend verify_result<block_facts> assess_block_contextual(
        const block& candidate,
        const block_contextual_context& context);

    friend verify_result<block_facts> validation_support::assess_block_spends_with_callbacks(
        const block& candidate,
        const block_spend_context& context,
        const void* coins,
        validation_support::coin_lookup_callback lookup_coin);

    block_facts(
        block_hash hash,
        hash256 merkle_root,
        std::size_t transaction_count,
        std::size_t serialized_size,
        std::size_t stripped_size,
        std::size_t weight,
        std::size_t legacy_sigop_cost,
        std::size_t sigop_cost,
        hash256 witness_merkle_root,
        bool has_witness,
        witness_commitment commitment,
        std::vector<transaction_facts> transactions,
        std::vector<spend_facts> spends = {},
        std::vector<spent_output> spent_outputs = {},
        std::vector<created_output> created_outputs = {},
        amount total_fees = amount{0},
        amount coinbase_output_value = amount{0}) : m_hash{hash},
                                                    m_merkle_root{merkle_root},
                                                    m_transaction_count{transaction_count},
                                                    m_serialized_size{serialized_size},
                                                    m_stripped_size{stripped_size},
                                                    m_weight{weight},
                                                    m_legacy_sigop_cost{legacy_sigop_cost},
                                                    m_sigop_cost{sigop_cost},
                                                    m_witness_merkle_root{witness_merkle_root},
                                                    m_has_witness{has_witness},
                                                    m_commitment{commitment},
                                                    m_transactions{std::move(transactions)},
                                                    m_spends{std::move(spends)},
                                                    m_spent_outputs{std::move(spent_outputs)},
                                                    m_created_outputs{std::move(created_outputs)},
                                                    m_total_fees{total_fees},
                                                    m_coinbase_output_value{coinbase_output_value}
    {
    }

    block_hash m_hash;
    hash256 m_merkle_root;
    std::size_t m_transaction_count{0};
    std::size_t m_serialized_size{0};
    std::size_t m_stripped_size{0};
    std::size_t m_weight{0};
    std::size_t m_legacy_sigop_cost{0};
    std::size_t m_sigop_cost{0};
    hash256 m_witness_merkle_root;
    bool m_has_witness{false};
    witness_commitment m_commitment;
    std::vector<transaction_facts> m_transactions;
    std::vector<spend_facts> m_spends;
    std::vector<spent_output> m_spent_outputs;
    std::vector<created_output> m_created_outputs;
    amount m_total_fees{amount{0}};
    amount m_coinbase_output_value{amount{0}};
};

// Expert building block for intrinsic block rules. Prefer verify(block) or the
// contextual verify(...) overloads for release-facing code.
[[nodiscard]] verify_result<block_facts> assess_block_intrinsic(
    const block& candidate,
    const block_local_context& context);

// Expert building block for contextual block rules after header ancestry has
// already been classified.
[[nodiscard]] verify_result<block_facts> assess_block_contextual(
    const block& candidate,
    const block_contextual_context& context);

[[nodiscard]] inline verify_result<block_facts> verify(const block& candidate)
{
    return assess_block_intrinsic(candidate, block_local_context{});
}

namespace validation_support {

// Support functions kept visible only because public templates need their
// contracts. The release-facing operation names are the free verify(...)
// overloads in this namespace's parent.

[[nodiscard]] verify_result<block_facts> invalid_block(validation_rejection rejection);
[[nodiscard]] verify_result<block_facts> failed_block(operation_error error);
[[nodiscard]] std::size_t transaction_sigop_cost(
    const transaction& tx,
    std::span<const coin> input_coins,
    verification_flags flags);

} // namespace validation_support

template <validation_coin_source Coins>
[[nodiscard]] verify_result<block_facts> assess_block_spends(
    const block& candidate,
    const block_spend_context& context,
    const Coins& coins)
{
    const auto lookup_coin{[](const void* state, const outpoint& point) {
        return validation_support::lookup_coin(*static_cast<const Coins*>(state), point);
    }};
    return validation_support::assess_block_spends_with_callbacks(
        candidate,
        context,
        &coins,
        lookup_coin);
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_BLOCK_H
