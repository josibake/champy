// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/spend_state_batch.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <optional>
#include <vector>

namespace {

CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(50, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeSpendTx(std::vector<COutPoint> prevouts)
{
    CMutableTransaction tx;
    tx.vin.reserve(prevouts.size());
    for (const COutPoint& prevout : prevouts) {
        tx.vin.emplace_back(prevout);
    }
    tx.vout.emplace_back(1, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

COutPoint OutPoint(uint8_t value, uint32_t index = 0)
{
    return COutPoint{Txid::FromUint256(uint256{value}), index};
}

Consensus::CoinSnapshot Coin(CAmount value)
{
    return Consensus::CoinSnapshot{.output = CTxOut{value, CScript{} << OP_TRUE}, .height = 3};
}

class FakeSpendState final : public Consensus::SpendLookupBackend {
public:
    std::map<COutPoint, Consensus::CoinSnapshot> coins;

    bool HaveCoin(const COutPoint& outpoint) const override
    {
        return coins.contains(outpoint);
    }

    std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const override
    {
        const auto coin{coins.find(outpoint)};
        if (coin == coins.end()) return std::nullopt;
        return coin->second;
    }
};

class RecordingBatchView final : public Consensus::SpendLookupBatchBackend {
public:
    std::vector<COutPoint> requested;
    std::vector<std::optional<Consensus::CoinSnapshot>> coins;

    std::vector<std::optional<Consensus::CoinSnapshot>> GetCoins(std::span<const COutPoint> outpoints) const override
    {
        auto& self{const_cast<RecordingBatchView&>(*this)};
        self.requested.assign(outpoints.begin(), outpoints.end());
        return coins;
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_spend_state_batch_tests)

BOOST_AUTO_TEST_CASE(extract_block_spent_output_lookups_skips_coinbase_and_keeps_input_coordinates)
{
    const COutPoint first{OutPoint(3, 0)};
    const COutPoint second{OutPoint(1, 1)};
    const COutPoint third{OutPoint(2, 0)};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        MakeSpendTx({first, second}),
        MakeSpendTx({third}),
    };

    const std::vector lookups{Consensus::ExtractBlockSpentOutputLookups(transactions)};

    BOOST_REQUIRE_EQUAL(lookups.size(), 3U);
    BOOST_CHECK(lookups[0].outpoint == first);
    BOOST_CHECK_EQUAL(lookups[0].transaction_index, 1U);
    BOOST_CHECK_EQUAL(lookups[0].input_index, 0U);
    BOOST_CHECK(lookups[1].outpoint == second);
    BOOST_CHECK_EQUAL(lookups[1].transaction_index, 1U);
    BOOST_CHECK_EQUAL(lookups[1].input_index, 1U);
    BOOST_CHECK(lookups[2].outpoint == third);
    BOOST_CHECK_EQUAL(lookups[2].transaction_index, 2U);
    BOOST_CHECK_EQUAL(lookups[2].input_index, 0U);
}

BOOST_AUTO_TEST_CASE(sort_block_spent_output_lookups_orders_by_outpoint_without_losing_coordinates)
{
    const std::vector<Consensus::BlockSpentOutputLookup> lookups{
        {.outpoint = OutPoint(3, 0), .transaction_index = 2, .input_index = 0},
        {.outpoint = OutPoint(1, 1), .transaction_index = 1, .input_index = 1},
        {.outpoint = OutPoint(1, 0), .transaction_index = 1, .input_index = 0},
    };

    const std::vector sorted{Consensus::SortBlockSpentOutputLookupsByOutpoint(lookups)};

    BOOST_REQUIRE_EQUAL(sorted.size(), 3U);
    BOOST_CHECK(sorted[0].outpoint == OutPoint(1, 0));
    BOOST_CHECK_EQUAL(sorted[0].transaction_index, 1U);
    BOOST_CHECK_EQUAL(sorted[0].input_index, 0U);
    BOOST_CHECK(sorted[1].outpoint == OutPoint(1, 1));
    BOOST_CHECK_EQUAL(sorted[1].transaction_index, 1U);
    BOOST_CHECK_EQUAL(sorted[1].input_index, 1U);
    BOOST_CHECK(sorted[2].outpoint == OutPoint(3, 0));
    BOOST_CHECK_EQUAL(sorted[2].transaction_index, 2U);
    BOOST_CHECK_EQUAL(sorted[2].input_index, 0U);

    BOOST_CHECK(lookups[0].outpoint == OutPoint(3, 0));
}

BOOST_AUTO_TEST_CASE(spend_state_batch_adapter_reads_coins_in_request_order)
{
    const COutPoint first{OutPoint(1)};
    const COutPoint missing{OutPoint(2)};
    const COutPoint second{OutPoint(3)};

    FakeSpendState spend_state;
    spend_state.coins.emplace(first, Consensus::CoinSnapshot{.output = CTxOut{11, CScript{} << OP_TRUE}, .height = 7});
    spend_state.coins.emplace(second, Consensus::CoinSnapshot{.output = CTxOut{33, CScript{} << OP_TRUE}, .height = 9, .is_coinbase = true});

    const Consensus::SpendLookupBatchBackendAdapter batch_view{spend_state};
    const std::vector<COutPoint> outpoints{first, missing, second};
    const std::vector coins{batch_view.GetCoins(outpoints)};

    BOOST_REQUIRE_EQUAL(coins.size(), 3U);
    BOOST_REQUIRE(coins[0]);
    BOOST_CHECK_EQUAL(coins[0]->output.nValue, 11);
    BOOST_CHECK(!coins[1]);
    BOOST_REQUIRE(coins[2]);
    BOOST_CHECK_EQUAL(coins[2]->output.nValue, 33);
    BOOST_CHECK(coins[2]->is_coinbase);
}

BOOST_AUTO_TEST_CASE(plan_block_spent_output_lookups_separates_external_and_intra_block_dependencies)
{
    const COutPoint external{OutPoint(9)};
    const CTransactionRef parent{MakeSpendTx({external})};
    const COutPoint parent_output{parent->GetHash(), 0};
    const CTransactionRef child{MakeSpendTx({parent_output, OutPoint(10)})};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        parent,
        child,
    };

    const Consensus::BlockSpentOutputLookupPlan plan{Consensus::PlanBlockSpentOutputLookups(transactions)};

    BOOST_REQUIRE_EQUAL(plan.external_lookups.size(), 2U);
    BOOST_CHECK(plan.external_lookups[0].outpoint == external);
    BOOST_CHECK_EQUAL(plan.external_lookups[0].transaction_index, 1U);
    BOOST_CHECK(plan.external_lookups[1].outpoint == OutPoint(10));
    BOOST_CHECK_EQUAL(plan.external_lookups[1].transaction_index, 2U);

    BOOST_REQUIRE_EQUAL(plan.intra_block_dependencies.size(), 1U);
    BOOST_CHECK(plan.intra_block_dependencies[0].lookup.outpoint == parent_output);
    BOOST_CHECK_EQUAL(plan.intra_block_dependencies[0].lookup.transaction_index, 2U);
    BOOST_CHECK_EQUAL(plan.intra_block_dependencies[0].lookup.input_index, 0U);
    BOOST_CHECK_EQUAL(plan.intra_block_dependencies[0].created_by_transaction, 1U);
    BOOST_CHECK(plan.intra_block_dependencies[0].source == Consensus::BlockSpentOutputSource::EarlierTransaction);
    BOOST_CHECK(plan.invalid_order_dependencies.empty());
}

BOOST_AUTO_TEST_CASE(plan_block_spent_output_lookups_identifies_same_or_later_transaction_dependencies)
{
    const CTransactionRef later{MakeSpendTx({OutPoint(11)})};
    const CTransactionRef earlier{MakeSpendTx({COutPoint{later->GetHash(), 0}})};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        earlier,
        later,
    };

    const Consensus::BlockSpentOutputLookupPlan plan{Consensus::PlanBlockSpentOutputLookups(transactions)};
    const COutPoint later_output{later->GetHash(), 0};

    BOOST_REQUIRE_EQUAL(plan.external_lookups.size(), 1U);
    BOOST_CHECK(plan.external_lookups[0].outpoint == OutPoint(11));
    BOOST_CHECK(plan.intra_block_dependencies.empty());
    BOOST_REQUIRE_EQUAL(plan.invalid_order_dependencies.size(), 1U);
    BOOST_CHECK(plan.invalid_order_dependencies[0].lookup.outpoint == later_output);
    BOOST_CHECK_EQUAL(plan.invalid_order_dependencies[0].lookup.transaction_index, 1U);
    BOOST_CHECK_EQUAL(plan.invalid_order_dependencies[0].created_by_transaction, 2U);
    BOOST_CHECK(plan.invalid_order_dependencies[0].source == Consensus::BlockSpentOutputSource::SameOrLaterTransaction);
}

BOOST_AUTO_TEST_CASE(lookup_block_spent_outputs_preserves_lookup_metadata)
{
    const std::vector<Consensus::BlockSpentOutputLookup> lookups{
        {.outpoint = OutPoint(1), .transaction_index = 1, .input_index = 0},
        {.outpoint = OutPoint(2), .transaction_index = 2, .input_index = 1},
    };
    RecordingBatchView batch_view;
    batch_view.coins = {
        Consensus::CoinSnapshot{.output = CTxOut{10, CScript{} << OP_TRUE}, .height = 3},
        std::nullopt,
    };

    const auto lookup_batch{Consensus::LookupBlockSpentOutputs(lookups, batch_view)};

    BOOST_REQUIRE_EQUAL(batch_view.requested.size(), 2U);
    BOOST_CHECK(batch_view.requested[0] == OutPoint(1));
    BOOST_CHECK(batch_view.requested[1] == OutPoint(2));
    BOOST_REQUIRE(lookup_batch);
    const auto& results{*lookup_batch};
    BOOST_REQUIRE_EQUAL(results.size(), 2U);
    BOOST_CHECK(results[0].lookup.outpoint == OutPoint(1));
    BOOST_CHECK_EQUAL(results[0].lookup.transaction_index, 1U);
    BOOST_CHECK_EQUAL(results[0].lookup.input_index, 0U);
    BOOST_REQUIRE(results[0].coin);
    BOOST_CHECK_EQUAL(results[0].coin->output.nValue, 10);
    BOOST_CHECK(results[1].lookup.outpoint == OutPoint(2));
    BOOST_CHECK(!results[1].coin);
}

BOOST_AUTO_TEST_CASE(lookup_block_spent_outputs_reports_short_backend_result)
{
    const std::vector<Consensus::BlockSpentOutputLookup> lookups{
        {.outpoint = OutPoint(1), .transaction_index = 1, .input_index = 0},
        {.outpoint = OutPoint(2), .transaction_index = 1, .input_index = 1},
    };
    RecordingBatchView batch_view;
    batch_view.coins = {Coin(10)};

    const auto lookup_batch{Consensus::LookupBlockSpentOutputs(lookups, batch_view)};

    BOOST_REQUIRE(!lookup_batch);
    BOOST_CHECK_EQUAL(lookup_batch.error().requested_count, 2U);
    BOOST_CHECK_EQUAL(lookup_batch.error().result_count, 1U);
}

BOOST_AUTO_TEST_CASE(lookup_block_spent_outputs_reports_long_backend_result)
{
    const std::vector<Consensus::BlockSpentOutputLookup> lookups{
        {.outpoint = OutPoint(1), .transaction_index = 1, .input_index = 0},
    };
    RecordingBatchView batch_view;
    batch_view.coins = {Coin(10), Coin(11)};

    const auto lookup_batch{Consensus::LookupBlockSpentOutputs(lookups, batch_view)};

    BOOST_REQUIRE(!lookup_batch);
    BOOST_CHECK_EQUAL(lookup_batch.error().requested_count, 1U);
    BOOST_CHECK_EQUAL(lookup_batch.error().result_count, 2U);
}

BOOST_AUTO_TEST_CASE(join_block_spent_outputs_batches_external_coins_and_intra_block_outputs)
{
    const COutPoint external_parent{OutPoint(20)};
    const COutPoint external_child{OutPoint(21)};
    const CTransactionRef parent{MakeSpendTx({external_parent})};
    const COutPoint parent_output{parent->GetHash(), 0};
    const CTransactionRef child{MakeSpendTx({parent_output, external_child})};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        parent,
        child,
    };

    RecordingBatchView batch_view;
    batch_view.coins = {
        Consensus::CoinSnapshot{.output = CTxOut{20, CScript{} << OP_TRUE}, .height = 5},
        Consensus::CoinSnapshot{.output = CTxOut{30, CScript{} << OP_TRUE}, .height = 6},
    };

    const Consensus::BlockSpentOutputJoin joined{Consensus::JoinBlockSpentOutputs(transactions, /*block_height=*/7, batch_view)};

    BOOST_CHECK(joined.status == Consensus::BlockSpentOutputJoinStatus::Complete);
    BOOST_CHECK(!joined.failed_lookup.has_value());
    BOOST_REQUIRE_EQUAL(batch_view.requested.size(), 2U);
    BOOST_CHECK(batch_view.requested[0] == external_parent);
    BOOST_CHECK(batch_view.requested[1] == external_child);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_transaction.size(), 3U);
    BOOST_CHECK(joined.input_coins_by_transaction[0].empty());
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_transaction[1].size(), 1U);
    BOOST_CHECK_EQUAL(joined.input_coins_by_transaction[1][0].output.nValue, 20);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_transaction[2].size(), 2U);
    BOOST_CHECK_EQUAL(joined.input_coins_by_transaction[2][0].output.nValue, 1);
    BOOST_CHECK_EQUAL(joined.input_coins_by_transaction[2][0].height, 7);
    BOOST_CHECK(!joined.input_coins_by_transaction[2][0].is_coinbase);
    BOOST_CHECK_EQUAL(joined.input_coins_by_transaction[2][1].output.nValue, 30);
}

BOOST_AUTO_TEST_CASE(join_block_spent_outputs_reports_short_backend_result_as_backend_mismatch)
{
    const COutPoint first{OutPoint(25)};
    const COutPoint second{OutPoint(26)};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        MakeSpendTx({first, second}),
    };

    RecordingBatchView batch_view;
    batch_view.coins = {Coin(10)};

    const Consensus::BlockSpentOutputJoin joined{Consensus::JoinBlockSpentOutputs(transactions, /*block_height=*/7, batch_view)};

    BOOST_CHECK(joined.status == Consensus::BlockSpentOutputJoinStatus::BackendMismatch);
    BOOST_CHECK(!joined.failed_lookup.has_value());
    BOOST_REQUIRE(joined.backend_mismatch.has_value());
    BOOST_CHECK_EQUAL(joined.backend_mismatch->requested_count, 2U);
    BOOST_CHECK_EQUAL(joined.backend_mismatch->result_count, 1U);
}

BOOST_AUTO_TEST_CASE(join_block_spent_outputs_reports_long_backend_result_as_backend_mismatch)
{
    const COutPoint prevout{OutPoint(27)};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        MakeSpendTx({prevout}),
    };

    RecordingBatchView batch_view;
    batch_view.coins = {Coin(10), Coin(11)};

    const Consensus::BlockSpentOutputJoin joined{Consensus::JoinBlockSpentOutputs(transactions, /*block_height=*/7, batch_view)};

    BOOST_CHECK(joined.status == Consensus::BlockSpentOutputJoinStatus::BackendMismatch);
    BOOST_CHECK(!joined.failed_lookup.has_value());
    BOOST_REQUIRE(joined.backend_mismatch.has_value());
    BOOST_CHECK_EQUAL(joined.backend_mismatch->requested_count, 1U);
    BOOST_CHECK_EQUAL(joined.backend_mismatch->result_count, 2U);
}

BOOST_AUTO_TEST_CASE(join_block_spent_outputs_reports_missing_external_input)
{
    const COutPoint missing{OutPoint(22)};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        MakeSpendTx({missing}),
    };

    RecordingBatchView batch_view;
    batch_view.coins = {std::nullopt};

    const Consensus::BlockSpentOutputJoin joined{Consensus::JoinBlockSpentOutputs(transactions, /*block_height=*/7, batch_view)};

    BOOST_CHECK(joined.status == Consensus::BlockSpentOutputJoinStatus::MissingOrSpent);
    BOOST_REQUIRE(joined.failed_lookup.has_value());
    BOOST_CHECK(joined.failed_lookup->outpoint == missing);
    BOOST_CHECK_EQUAL(joined.failed_lookup->transaction_index, 1U);
    BOOST_CHECK_EQUAL(joined.failed_lookup->input_index, 0U);
}

BOOST_AUTO_TEST_CASE(join_block_spent_outputs_reports_duplicate_spend_before_validation)
{
    const COutPoint prevout{OutPoint(23)};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        MakeSpendTx({prevout}),
        MakeSpendTx({prevout}),
    };

    RecordingBatchView batch_view;
    batch_view.coins = {
        Consensus::CoinSnapshot{.output = CTxOut{20, CScript{} << OP_TRUE}, .height = 5},
        Consensus::CoinSnapshot{.output = CTxOut{20, CScript{} << OP_TRUE}, .height = 5},
    };

    const Consensus::BlockSpentOutputJoin joined{Consensus::JoinBlockSpentOutputs(transactions, /*block_height=*/7, batch_view)};

    BOOST_CHECK(joined.status == Consensus::BlockSpentOutputJoinStatus::DuplicateSpend);
    BOOST_REQUIRE(joined.failed_lookup.has_value());
    BOOST_CHECK(joined.failed_lookup->outpoint == prevout);
    BOOST_CHECK_EQUAL(joined.failed_lookup->transaction_index, 2U);
    BOOST_CHECK_EQUAL(joined.failed_lookup->input_index, 0U);
}

BOOST_AUTO_TEST_CASE(join_block_spent_outputs_reports_same_or_later_transaction_spend)
{
    const CTransactionRef later{MakeSpendTx({OutPoint(24)})};
    const CTransactionRef earlier{MakeSpendTx({COutPoint{later->GetHash(), 0}})};
    const std::vector<CTransactionRef> transactions{
        MakeCoinbase(),
        earlier,
        later,
    };

    RecordingBatchView batch_view;
    batch_view.coins = {
        Consensus::CoinSnapshot{.output = CTxOut{20, CScript{} << OP_TRUE}, .height = 5},
    };

    const Consensus::BlockSpentOutputJoin joined{Consensus::JoinBlockSpentOutputs(transactions, /*block_height=*/7, batch_view)};

    BOOST_CHECK(joined.status == Consensus::BlockSpentOutputJoinStatus::InvalidSpendOrder);
    BOOST_REQUIRE(joined.failed_lookup.has_value());
    const COutPoint later_output{later->GetHash(), 0};
    BOOST_CHECK(joined.failed_lookup->outpoint == later_output);
    BOOST_CHECK_EQUAL(joined.failed_lookup->transaction_index, 1U);
    BOOST_CHECK_EQUAL(joined.failed_lookup->input_index, 0U);
}

BOOST_AUTO_TEST_SUITE_END()
