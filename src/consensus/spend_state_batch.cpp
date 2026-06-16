// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/spend_state_batch.h>

#include <consensus/predicates.h>

#include <algorithm>
#include <cassert>
#include <map>
#include <optional>
#include <utility>

namespace Consensus {

namespace {

struct PendingBlockSpentOutputFailure {
    BlockSpentOutputJoinStatus status{BlockSpentOutputJoinStatus::Complete};
    BlockSpentOutputLookup lookup;
};

bool LookupBefore(const BlockSpentOutputLookup& a, const BlockSpentOutputLookup& b)
{
    return a.transaction_index < b.transaction_index ||
           (a.transaction_index == b.transaction_index && a.input_index < b.input_index);
}

void UpdateFailure(
    BlockSpentOutputJoinStatus status,
    const BlockSpentOutputLookup& lookup,
    std::optional<PendingBlockSpentOutputFailure>& failure)
{
    if (!failure || LookupBefore(lookup, failure->lookup)) {
        failure = PendingBlockSpentOutputFailure{.status = status, .lookup = lookup};
    }
}

std::optional<BlockSpentOutputLookup> FirstDuplicateSpend(std::span<const CTransactionRef> transactions)
{
    std::map<COutPoint, BlockSpentOutputLookup> seen_spends;
    for (const BlockSpentOutputLookup& lookup : ExtractBlockSpentOutputLookups(transactions)) {
        const auto [_, inserted]{seen_spends.emplace(lookup.outpoint, lookup)};
        if (!inserted) return lookup;
    }
    return std::nullopt;
}

} // namespace

std::vector<std::optional<CoinSnapshot>> SpendLookupBatchBackendAdapter::GetCoins(std::span<const COutPoint> outpoints) const
{
    std::vector<std::optional<CoinSnapshot>> coins;
    coins.reserve(outpoints.size());
    for (const COutPoint& outpoint : outpoints) {
        coins.push_back(m_spend_state.GetCoin(outpoint));
    }
    return coins;
}

std::vector<BlockSpentOutputLookup> ExtractBlockSpentOutputLookups(std::span<const CTransactionRef> transactions)
{
    std::vector<BlockSpentOutputLookup> lookups;
    for (std::size_t transaction_index{0}; transaction_index < transactions.size(); ++transaction_index) {
        const CTransaction& tx{*transactions[transaction_index]};
        if (IsCoinbase(tx)) continue;

        lookups.reserve(lookups.size() + tx.vin.size());
        for (std::size_t input_index{0}; input_index < tx.vin.size(); ++input_index) {
            lookups.push_back({
                .outpoint = tx.vin[input_index].prevout,
                .transaction_index = transaction_index,
                .input_index = input_index,
            });
        }
    }
    return lookups;
}

BlockSpentOutputLookupPlan PlanBlockSpentOutputLookups(std::span<const CTransactionRef> transactions)
{
    std::map<COutPoint, std::size_t> created_by_transaction;
    for (std::size_t transaction_index{0}; transaction_index < transactions.size(); ++transaction_index) {
        const CTransaction& tx{*transactions[transaction_index]};
        const Txid txid{tx.GetHash()};
        for (std::size_t output_index{0}; output_index < tx.vout.size(); ++output_index) {
            if (IsUnspendable(tx.vout[output_index])) continue;
            created_by_transaction.emplace(COutPoint{txid, static_cast<uint32_t>(output_index)}, transaction_index);
        }
    }

    BlockSpentOutputLookupPlan plan;
    for (BlockSpentOutputLookup lookup : ExtractBlockSpentOutputLookups(transactions)) {
        const auto created{created_by_transaction.find(lookup.outpoint)};
        if (created == created_by_transaction.end()) {
            plan.external_lookups.push_back(std::move(lookup));
            continue;
        }

        BlockSpentOutputDependency dependency{
            .lookup = std::move(lookup),
            .created_by_transaction = created->second,
        };
        if (created->second < dependency.lookup.transaction_index) {
            dependency.source = BlockSpentOutputSource::EarlierTransaction;
            plan.intra_block_dependencies.push_back(std::move(dependency));
        } else {
            dependency.source = BlockSpentOutputSource::SameOrLaterTransaction;
            plan.invalid_order_dependencies.push_back(std::move(dependency));
        }
    }
    return plan;
}

std::vector<BlockSpentOutputLookup> SortBlockSpentOutputLookupsByOutpoint(std::span<const BlockSpentOutputLookup> lookups)
{
    std::vector<BlockSpentOutputLookup> sorted{lookups.begin(), lookups.end()};
    std::ranges::sort(sorted, [](const BlockSpentOutputLookup& a, const BlockSpentOutputLookup& b) {
        return a.outpoint < b.outpoint;
    });
    return sorted;
}

std::vector<COutPoint> OutpointsForBlockSpentOutputLookups(std::span<const BlockSpentOutputLookup> lookups)
{
    std::vector<COutPoint> outpoints;
    outpoints.reserve(lookups.size());
    for (const BlockSpentOutputLookup& lookup : lookups) {
        outpoints.push_back(lookup.outpoint);
    }
    return outpoints;
}

BlockSpentOutputLookupBatch LookupBlockSpentOutputs(std::span<const BlockSpentOutputLookup> lookups, const SpendLookupBatchBackend& spend_state)
{
    const std::vector<COutPoint> outpoints{OutpointsForBlockSpentOutputLookups(lookups)};
    std::vector<std::optional<CoinSnapshot>> coins{spend_state.GetCoins(outpoints)};
    if (coins.size() != lookups.size()) {
        return Unexpected<BlockSpentOutputBatchMismatch>{BlockSpentOutputBatchMismatch{
            .requested_count = lookups.size(),
            .result_count = coins.size(),
        }};
    }

    std::vector<BlockSpentOutputLookupResult> results;
    results.reserve(lookups.size());
    for (std::size_t i{0}; i < lookups.size(); ++i) {
        results.push_back({
            .lookup = lookups[i],
            .coin = std::move(coins[i]),
        });
    }
    return results;
}

BlockSpentOutputJoin JoinBlockSpentOutputs(std::span<const CTransactionRef> transactions, int block_height, const SpendLookupBatchBackend& spend_state)
{
    BlockSpentOutputJoin joined;
    joined.input_coins_by_transaction.resize(transactions.size());

    std::vector<std::vector<std::optional<CoinSnapshot>>> input_coins;
    input_coins.resize(transactions.size());
    for (std::size_t transaction_index{0}; transaction_index < transactions.size(); ++transaction_index) {
        const CTransaction& tx{*transactions[transaction_index]};
        if (!IsCoinbase(tx)) {
            input_coins[transaction_index].resize(tx.vin.size());
        }
    }

    const BlockSpentOutputLookupPlan plan{PlanBlockSpentOutputLookups(transactions)};
    std::optional<PendingBlockSpentOutputFailure> failure;

    if (const std::optional<BlockSpentOutputLookup> duplicate{FirstDuplicateSpend(transactions)}) {
        UpdateFailure(BlockSpentOutputJoinStatus::DuplicateSpend, *duplicate, failure);
    }

    if (!plan.invalid_order_dependencies.empty()) {
        UpdateFailure(BlockSpentOutputJoinStatus::InvalidSpendOrder, plan.invalid_order_dependencies.front().lookup, failure);
    }

    if (failure) {
        joined.status = failure->status;
        joined.failed_lookup = failure->lookup;
        return joined;
    }

    auto lookup_results{LookupBlockSpentOutputs(plan.external_lookups, spend_state)};
    if (!lookup_results) {
        joined.status = BlockSpentOutputJoinStatus::BackendMismatch;
        joined.backend_mismatch = lookup_results.error();
        return joined;
    }

    for (BlockSpentOutputLookupResult& result : *lookup_results) {
        if (!result.coin) {
            UpdateFailure(BlockSpentOutputJoinStatus::MissingOrSpent, result.lookup, failure);
            continue;
        }
        input_coins[result.lookup.transaction_index][result.lookup.input_index] = std::move(result.coin);
    }

    for (const BlockSpentOutputDependency& dependency : plan.intra_block_dependencies) {
        const CTransaction& created_tx{*transactions[dependency.created_by_transaction]};
        assert(dependency.lookup.outpoint.n < created_tx.vout.size());
        input_coins[dependency.lookup.transaction_index][dependency.lookup.input_index] = CoinSnapshot{
            .output = created_tx.vout[dependency.lookup.outpoint.n],
            .height = block_height,
            .is_coinbase = IsCoinbase(created_tx),
        };
    }

    if (failure) {
        joined.status = failure->status;
        joined.failed_lookup = failure->lookup;
        return joined;
    }

    for (std::size_t transaction_index{0}; transaction_index < input_coins.size(); ++transaction_index) {
        joined.input_coins_by_transaction[transaction_index].reserve(input_coins[transaction_index].size());
        for (std::size_t input_index{0}; input_index < input_coins[transaction_index].size(); ++input_index) {
            assert(input_coins[transaction_index][input_index].has_value());
            joined.input_coins_by_transaction[transaction_index].push_back(std::move(*input_coins[transaction_index][input_index]));
        }
    }

    return joined;
}

} // namespace Consensus
