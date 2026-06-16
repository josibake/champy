// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_VERIFY_H
#define BITCOIN_BITCOIN_VALIDATION_VERIFY_H

#include <bitcoin/validation/block.h>
#include <bitcoin/validation/context.h>
#include <bitcoin/validation/header.h>
#include <bitcoin/validation/result.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

namespace bitcoin {

struct block_validation_context {
    consensus_params consensus;
    block_limits limits;
    block_deployments deployments;
    deployment_state spend_deployments;
    locktime_flags locktime;
    script_context scripts;
    amount subsidy{amount{0}};
    std::int32_t coinbase_maturity{100};
    std::size_t max_sigop_cost{80'000};
};

namespace validation_support {

[[nodiscard]] inline operation_result<block_time> locktime_cutoff(median_time_past previous_mtp)
{
    const auto seconds{
        std::chrono::duration_cast<std::chrono::seconds>(
            previous_mtp.value().time_since_epoch())
            .count()};
    if (seconds < 0 ||
        seconds > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return operation_result<block_time>::failed(operation_error::make(
            operation_error_code::unsupported_operation,
            "previous median time cannot be represented as a block time"));
    }
    return operation_result<block_time>::ok(block_time{static_cast<std::uint32_t>(seconds)});
}

} // namespace validation_support

[[nodiscard]] inline verify_result<block_facts> verify(
    const block& candidate_block,
    const validated_header_context& ancestor_context,
    validation_time time,
    const block_validation_context& context)
{
    return verify_exception_boundary<block_facts>([&]() -> verify_result<block_facts> {
        const auto& header_context{ancestor_context.context()};
        const auto previous_mtp{header_context.previous_median_time_past};
        const auto locktime_cutoff{validation_support::locktime_cutoff(previous_mtp)};
        if (locktime_cutoff.has_error()) {
            return verify_result<block_facts>::failed(locktime_cutoff.assume_error());
        }

        const auto header_result{verify(
            candidate_block.header(),
            ancestor_context,
            time,
            context.consensus)};
        if (header_result.has_error()) {
            return verify_result<block_facts>::failed(header_result.assume_error());
        }
        if (!header_result.assume_value().accepted()) {
            return verify_result<block_facts>::ok(validation_decision<block_facts>::invalid(
                header_result.assume_value().assume_rejection()));
        }

        const block_contextual_context contextual{
            .local = block_local_context{.limits = context.limits},
            .height = header_context.height,
            .locktime_cutoff = locktime_cutoff.assume_value(),
            .deployments = context.deployments,
        };
        return assess_block_contextual(candidate_block, contextual);
    },
                                                  "block contextual validation exhausted resources", operation_error_code::internal_bug, "block contextual validation threw an exception");
}

template <chain_view Chain>
[[nodiscard]] evidence_verify_result<block_facts> verify(
    const block& candidate_block,
    const Chain& genesis_to_parent,
    validation_time time,
    const block_validation_context& context)
{
    auto ancestor_context{validate_header_context(genesis_to_parent, context.consensus)};
    if (!ancestor_context.has_value()) {
        if (ancestor_context.has_invalid_evidence()) {
            return evidence_verify_result<block_facts>::invalid(
                ancestor_context.assume_invalid_evidence());
        }
        return evidence_verify_result<block_facts>::failed(ancestor_context.assume_error());
    }

    auto result{verify(candidate_block, ancestor_context.assume_value(), time, context)};
    if (result.has_error()) {
        return evidence_verify_result<block_facts>::failed(result.assume_error());
    }
    return evidence_verify_result<block_facts>::checked(std::move(result).assume_value());
}

template <validation_coin_source Coins>
[[nodiscard]] verify_result<block_facts> verify(
    const block& candidate_block,
    const validated_header_context& ancestor_context,
    validation_time time,
    const block_validation_context& context,
    const Coins& coins)
{
    return verify_exception_boundary<block_facts>([&]() -> verify_result<block_facts> {
        const auto& header_context{ancestor_context.context()};
        const auto previous_mtp{header_context.previous_median_time_past};
        const auto contextual_result{verify(candidate_block, ancestor_context, time, context)};
        if (contextual_result.has_error() || !contextual_result.assume_value().accepted()) {
            return contextual_result;
        }

        const block_spend_context spend{
            .local = block_local_context{.limits = context.limits},
            .spend = spend_context{
                .height = header_context.height,
                .previous_median_time_past = previous_mtp,
                .limits = context.limits.transactions.limits,
                .locktime = context.locktime,
                .coinbase_maturity = context.coinbase_maturity,
            },
            .scripts = context.scripts,
            .deployments = context.spend_deployments,
            .subsidy = context.subsidy,
            .max_sigop_cost = context.max_sigop_cost,
        };
        return assess_block_spends(candidate_block, spend, coins);
    },
                                                  "block validation exhausted resources", operation_error_code::internal_bug, "block validation threw an exception");
}

template <chain_view Chain, validation_coin_source Coins>
[[nodiscard]] evidence_verify_result<block_facts> verify(
    const block& candidate_block,
    const Chain& genesis_to_parent,
    validation_time time,
    const block_validation_context& context,
    const Coins& coins)
{
    auto ancestor_context{validate_header_context(genesis_to_parent, context.consensus)};
    if (!ancestor_context.has_value()) {
        if (ancestor_context.has_invalid_evidence()) {
            return evidence_verify_result<block_facts>::invalid(
                ancestor_context.assume_invalid_evidence());
        }
        return evidence_verify_result<block_facts>::failed(ancestor_context.assume_error());
    }

    auto result{verify(candidate_block, ancestor_context.assume_value(), time, context, coins)};
    if (result.has_error()) {
        return evidence_verify_result<block_facts>::failed(result.assume_error());
    }
    return evidence_verify_result<block_facts>::checked(std::move(result).assume_value());
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_VERIFY_H
