// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SPEND_STATE_BATCH_H
#define BITCOIN_CONSENSUS_SPEND_STATE_BATCH_H

#include <consensus/expected.h>
#include <consensus/spend_state.h>
#include <primitives/transaction.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Consensus {

struct BlockSpentOutputLookup {
    COutPoint outpoint;
    std::size_t transaction_index{0};
    std::size_t input_index{0};
};

struct BlockSpentOutputLookupResult {
    BlockSpentOutputLookup lookup;
    std::optional<CoinSnapshot> coin;
};

enum class BlockSpentOutputSource {
    External,
    EarlierTransaction,
    SameOrLaterTransaction,
};

enum class BlockSpentOutputJoinStatus {
    Complete,
    MissingOrSpent,
    DuplicateSpend,
    InvalidSpendOrder,
    BackendMismatch,
};

struct BlockSpentOutputBatchMismatch {
    std::size_t requested_count{0};
    std::size_t result_count{0};
};

struct BlockSpentOutputDependency {
    BlockSpentOutputLookup lookup;
    std::size_t created_by_transaction{0};
    BlockSpentOutputSource source{BlockSpentOutputSource::External};
};

struct BlockSpentOutputLookupPlan {
    std::vector<BlockSpentOutputLookup> external_lookups;
    std::vector<BlockSpentOutputDependency> intra_block_dependencies;
    std::vector<BlockSpentOutputDependency> invalid_order_dependencies;
};

struct BlockSpentOutputJoin {
    BlockSpentOutputJoinStatus status{BlockSpentOutputJoinStatus::Complete};
    std::optional<BlockSpentOutputLookup> failed_lookup;
    // One-to-one with the block transaction list. Coinbase entries are empty.
    std::vector<std::vector<CoinSnapshot>> input_coins_by_transaction;
    std::optional<BlockSpentOutputBatchMismatch> backend_mismatch;
};

using BlockSpentOutputLookupBatch = Expected<std::vector<BlockSpentOutputLookupResult>, BlockSpentOutputBatchMismatch>;

class SpendLookupBatchBackend {
public:
    virtual ~SpendLookupBatchBackend() = default;

    // Results must be one-to-one and in the same order as the requested
    // outpoints. Missing coins are represented by std::nullopt.
    [[nodiscard]] virtual std::vector<std::optional<CoinSnapshot>> GetCoins(std::span<const COutPoint> outpoints) const = 0;
};

using SpendStateBatchView = SpendLookupBatchBackend;

class SpendLookupBatchBackendAdapter final : public SpendLookupBatchBackend {
public:
    explicit SpendLookupBatchBackendAdapter(const SpendLookupBackend& spend_state) : m_spend_state{spend_state} {}

    [[nodiscard]] std::vector<std::optional<CoinSnapshot>> GetCoins(std::span<const COutPoint> outpoints) const override;

private:
    const SpendLookupBackend& m_spend_state;
};

using SpendStateBatchViewAdapter = SpendLookupBatchBackendAdapter;

[[nodiscard]] std::vector<BlockSpentOutputLookup> ExtractBlockSpentOutputLookups(std::span<const CTransactionRef> transactions);
[[nodiscard]] BlockSpentOutputLookupPlan PlanBlockSpentOutputLookups(std::span<const CTransactionRef> transactions);
[[nodiscard]] std::vector<BlockSpentOutputLookup> SortBlockSpentOutputLookupsByOutpoint(std::span<const BlockSpentOutputLookup> lookups);
[[nodiscard]] std::vector<COutPoint> OutpointsForBlockSpentOutputLookups(std::span<const BlockSpentOutputLookup> lookups);
[[nodiscard]] BlockSpentOutputLookupBatch LookupBlockSpentOutputs(std::span<const BlockSpentOutputLookup> lookups, const SpendLookupBatchBackend& spend_state);
[[nodiscard]] BlockSpentOutputJoin JoinBlockSpentOutputs(std::span<const CTransactionRef> transactions, int block_height, const SpendLookupBatchBackend& spend_state);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SPEND_STATE_BATCH_H
