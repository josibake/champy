// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <consensus/consensus.h>
#include <consensus/segment_spend.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

uint256 Uint(int value)
{
    return ArithToUint256(arith_uint256{static_cast<uint64_t>(value)});
}

CTransactionRef MakeCoinbase(CAmount value = 50)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeSpend(const COutPoint& prevout, CAmount value = 49, uint32_t sequence = CTxIn::SEQUENCE_FINAL)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.resize(1);
    tx.vin[0].prevout = prevout;
    tx.vin[0].nSequence = sequence;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

Consensus::SegmentBlockContext BlockContext(int height)
{
    return {
        .hash = Uint(height),
        .parent_hash = height > 0 ? Uint(height - 1) : uint256{},
        .height = height,
        .previous_median_time_past = height * 100,
    };
}

class MapBatchView final : public Consensus::SegmentSpendBatchView {
public:
    std::map<COutPoint, Consensus::SegmentCoinSnapshot> coins;

    std::vector<std::optional<Consensus::SegmentCoinSnapshot>> GetCoins(std::span<const COutPoint> outpoints) const override
    {
        std::vector<std::optional<Consensus::SegmentCoinSnapshot>> result;
        result.reserve(outpoints.size());
        for (const COutPoint& outpoint : outpoints) {
            const auto coin{coins.find(outpoint)};
            result.push_back(coin == coins.end() ? std::nullopt : std::optional{coin->second});
        }
        return result;
    }
};

class CoinBatchView final : public Consensus::SpendLookupBatchBackend {
public:
    std::map<COutPoint, Consensus::CoinSnapshot> coins;

    std::vector<std::optional<Consensus::CoinSnapshot>> GetCoins(std::span<const COutPoint> outpoints) const override
    {
        std::vector<std::optional<Consensus::CoinSnapshot>> result;
        result.reserve(outpoints.size());
        for (const COutPoint& outpoint : outpoints) {
            const auto coin{coins.find(outpoint)};
            result.push_back(coin == coins.end() ? std::nullopt : std::optional{coin->second});
        }
        return result;
    }
};

class MapSequenceLockTimeView final : public Consensus::SequenceLockTimeView {
public:
    std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint;

    int64_t PreviousMedianTimePast(const COutPoint& outpoint, int) const override
    {
        const auto value{previous_median_time_past_by_outpoint.find(outpoint)};
        return value == previous_median_time_past_by_outpoint.end() ? 0 : value->second;
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_segment_spend_tests)

BOOST_AUTO_TEST_CASE(segment_spend_batch_adapter_enriches_coins_with_sequence_lock_times)
{
    const COutPoint prevout{Txid::FromUint256(Uint(90)), 0};
    CoinBatchView coins;
    coins.coins.emplace(prevout, Consensus::CoinSnapshot{
        .output = CTxOut{50, CScript{} << OP_TRUE},
        .height = 7,
        .is_coinbase = false,
    });
    MapSequenceLockTimeView sequence_lock_times;
    sequence_lock_times.previous_median_time_past_by_outpoint.emplace(prevout, 700);
    const Consensus::SegmentSpendBatchViewAdapter adapter{coins, sequence_lock_times};

    const std::vector<COutPoint> outpoints{prevout};
    const std::vector<std::optional<Consensus::SegmentCoinSnapshot>> resolved{adapter.GetCoins(outpoints)};

    BOOST_REQUIRE_EQUAL(resolved.size(), 1U);
    BOOST_REQUIRE(resolved[0]);
    BOOST_CHECK_EQUAL(resolved[0]->coin.height, 7);
    BOOST_CHECK_EQUAL(resolved[0]->coin.output.nValue, 50);
    BOOST_CHECK_EQUAL(resolved[0]->previous_median_time_past, 700);
}

BOOST_AUTO_TEST_CASE(segment_spend_join_resolves_outputs_created_by_earlier_blocks)
{
    const CTransactionRef coinbase{MakeCoinbase()};
    const COutPoint created{coinbase->GetHash(), 0};
    const CTransactionRef spend{MakeSpend(created)};

    const std::vector<CTransactionRef> block0{coinbase};
    const std::vector<CTransactionRef> block1{MakeCoinbase(49), spend};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(1), .transactions = block0},
        {.context = BlockContext(2), .transactions = block1},
    };

    const MapBatchView external_coins;
    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    BOOST_CHECK(joined.status == Consensus::SegmentSpentOutputJoinStatus::Complete);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_block.size(), 2U);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_block[1][1].size(), 1U);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[1][1][0].coin.output.nValue, 50);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[1][1][0].coin.height, 1);
    BOOST_CHECK(joined.input_coins_by_block[1][1][0].coin.is_coinbase);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[1][1][0].previous_median_time_past, 100);
}

BOOST_AUTO_TEST_CASE(segment_spend_join_resolves_external_outputs_through_batch_view)
{
    const COutPoint prevout{Txid::FromUint256(Uint(100)), 0};
    const std::vector<CTransactionRef> block{MakeCoinbase(), MakeSpend(prevout, 17)};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(10), .transactions = block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(prevout, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{19, CScript{} << OP_TRUE},
            .height = 3,
            .is_coinbase = false,
        },
        .previous_median_time_past = 300,
    });

    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    BOOST_CHECK(joined.status == Consensus::SegmentSpentOutputJoinStatus::Complete);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_block[0][1].size(), 1U);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[0][1][0].coin.output.nValue, 19);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[0][1][0].coin.height, 3);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[0][1][0].previous_median_time_past, 300);
}

BOOST_AUTO_TEST_CASE(segment_spend_join_rejects_same_or_later_segment_outputs)
{
    const CTransactionRef coinbase{MakeCoinbase(51)};
    const COutPoint future_output{coinbase->GetHash(), 0};
    const std::vector<CTransactionRef> block0{MakeCoinbase(), MakeSpend(future_output)};
    const std::vector<CTransactionRef> block1{coinbase};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(1), .transactions = block0},
        {.context = BlockContext(2), .transactions = block1},
    };

    const MapBatchView external_coins;
    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    BOOST_CHECK(joined.status == Consensus::SegmentSpentOutputJoinStatus::InvalidSpendOrder);
    BOOST_REQUIRE(joined.failed_lookup);
    BOOST_CHECK_EQUAL(joined.failed_lookup->block_index, 0U);
    BOOST_CHECK_EQUAL(joined.failed_lookup->lookup.transaction_index, 1U);
}

BOOST_AUTO_TEST_CASE(segment_spend_join_rejects_duplicate_spends_across_blocks)
{
    const COutPoint prevout{Txid::FromUint256(Uint(200)), 0};
    const std::vector<CTransactionRef> block0{MakeCoinbase(), MakeSpend(prevout)};
    const std::vector<CTransactionRef> block1{MakeCoinbase(), MakeSpend(prevout)};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(1), .transactions = block0},
        {.context = BlockContext(2), .transactions = block1},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(prevout, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 0,
            .is_coinbase = false,
        },
        .previous_median_time_past = 0,
    });

    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    BOOST_CHECK(joined.status == Consensus::SegmentSpentOutputJoinStatus::DuplicateSpend);
    BOOST_REQUIRE(joined.failed_lookup);
    BOOST_CHECK_EQUAL(joined.failed_lookup->block_index, 1U);
}

BOOST_AUTO_TEST_CASE(segment_spend_join_treats_future_duplicate_as_external_when_parent_coin_exists)
{
    const CTransactionRef duplicate_tx{MakeCoinbase(50)};
    const COutPoint duplicate_outpoint{duplicate_tx->GetHash(), 0};
    const std::vector<CTransactionRef> first_block{MakeCoinbase(1), MakeSpend(duplicate_outpoint, 49)};
    const std::vector<CTransactionRef> second_block{duplicate_tx};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(200), .transactions = first_block},
        {.context = BlockContext(201), .transactions = second_block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(duplicate_outpoint, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = true,
        },
        .previous_median_time_past = 100,
    });

    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    BOOST_CHECK(joined.status == Consensus::SegmentSpentOutputJoinStatus::Complete);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_block[0][1].size(), 1U);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[0][1][0].coin.output.nValue, 50);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[0][1][0].coin.height, 1);
}

BOOST_AUTO_TEST_CASE(segment_spend_join_allows_spend_recreate_spend_as_two_ordered_coins)
{
    const CTransactionRef duplicate_tx{MakeCoinbase(50)};
    const COutPoint duplicate_outpoint{duplicate_tx->GetHash(), 0};
    const std::vector<CTransactionRef> first_block{MakeCoinbase(1), MakeSpend(duplicate_outpoint, 49)};
    const std::vector<CTransactionRef> second_block{duplicate_tx};
    const std::vector<CTransactionRef> third_block{MakeCoinbase(1), MakeSpend(duplicate_outpoint, 49)};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(200), .transactions = first_block},
        {.context = BlockContext(201), .transactions = second_block},
        {.context = BlockContext(202), .transactions = third_block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(duplicate_outpoint, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = true,
        },
        .previous_median_time_past = 100,
    });

    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    BOOST_CHECK(joined.status == Consensus::SegmentSpentOutputJoinStatus::Complete);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_block[0][1].size(), 1U);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[0][1][0].coin.height, 1);
    BOOST_REQUIRE_EQUAL(joined.input_coins_by_block[2][1].size(), 1U);
    BOOST_CHECK_EQUAL(joined.input_coins_by_block[2][1][0].coin.height, 201);
    BOOST_CHECK(joined.input_coins_by_block[2][1][0].coin.is_coinbase);
}

BOOST_AUTO_TEST_CASE(resolved_segment_block_spend_produces_effects_and_script_plans)
{
    const COutPoint prevout{Txid::FromUint256(Uint(250)), 0};
    std::vector<CTransactionRef> block{MakeCoinbase(3), MakeSpend(prevout, 39)};
    Consensus::SegmentBlockContext context{BlockContext(2)};
    context.block_subsidy = 0;
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = context, .transactions = block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(prevout, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{42, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = false,
        },
        .previous_median_time_past = 100,
    });
    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    auto spend{Consensus::ValidateResolvedSegmentBlockSpend(
        blocks[0],
        joined,
        /*block_index=*/0,
        Consensus::ScriptCheckPlanCollection::Collect)};

    BOOST_REQUIRE(spend);
    BOOST_CHECK_EQUAL(spend->effects.fees, 3);
    BOOST_CHECK_EQUAL(spend->effects.inputs, 2);
    BOOST_REQUIRE_EQUAL(spend->effects.transaction_effects.size(), 2U);
    BOOST_REQUIRE_EQUAL(spend->script_checks.size(), 1U);
    BOOST_CHECK(spend->script_checks[0].tx == block[1]);
    BOOST_REQUIRE_EQUAL(spend->script_checks[0].spent_outputs.size(), 1U);
    BOOST_CHECK_EQUAL(spend->script_checks[0].spent_outputs[0].nValue, 42);
}

BOOST_AUTO_TEST_CASE(resolved_segment_block_spend_uses_joined_sequence_lock_times)
{
    const COutPoint prevout{Txid::FromUint256(Uint(260)), 0};
    const uint32_t sequence{CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1};
    std::vector<CTransactionRef> block{MakeCoinbase(3), MakeSpend(prevout, 39, sequence)};
    Consensus::SegmentBlockContext context{BlockContext(10)};
    context.previous_median_time_past = 1000;
    context.block_subsidy = 0;
    context.spend_options.locktime_flags = LOCKTIME_VERIFY_SEQUENCE;
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = context, .transactions = block},
    };

    auto validate_with_previous_mtp{[&](int64_t previous_median_time_past) {
        MapBatchView external_coins;
        external_coins.coins.emplace(prevout, Consensus::SegmentCoinSnapshot{
            .coin = Consensus::CoinSnapshot{
                .output = CTxOut{42, CScript{} << OP_TRUE},
                .height = 1,
                .is_coinbase = false,
            },
            .previous_median_time_past = previous_median_time_past,
        });
        const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};
        return Consensus::ValidateResolvedSegmentBlockSpend(
            blocks[0],
            joined,
            /*block_index=*/0,
            Consensus::ScriptCheckPlanCollection::Skip);
    }};

    BOOST_CHECK(validate_with_previous_mtp(/*previous_median_time_past=*/100));

    const auto nonfinal{validate_with_previous_mtp(/*previous_median_time_past=*/900)};
    BOOST_REQUIRE(!nonfinal);
    BOOST_CHECK_EQUAL(nonfinal.error().reject_reason, "bad-txns-nonfinal");
}

BOOST_AUTO_TEST_CASE(resolved_segment_block_spend_preserves_coinbase_reward_failure)
{
    const COutPoint prevout{Txid::FromUint256(Uint(270)), 0};
    std::vector<CTransactionRef> block{MakeCoinbase(2), MakeSpend(prevout, 41)};
    Consensus::SegmentBlockContext context{BlockContext(2)};
    context.block_subsidy = 0;
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = context, .transactions = block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(prevout, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{42, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = false,
        },
        .previous_median_time_past = 100,
    });
    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};

    const auto spend{Consensus::ValidateResolvedSegmentBlockSpend(
        blocks[0],
        joined,
        /*block_index=*/0,
        Consensus::ScriptCheckPlanCollection::Skip)};

    BOOST_REQUIRE(!spend);
    BOOST_CHECK_EQUAL(spend.error().reject_reason, "bad-cb-amount");
}

BOOST_AUTO_TEST_CASE(segment_spend_validation_rejects_parent_unspent_output_overwrite)
{
    const CTransactionRef duplicate_tx{MakeCoinbase(50)};
    const COutPoint duplicate_outpoint{duplicate_tx->GetHash(), 0};
    const std::vector<CTransactionRef> block{duplicate_tx};
    Consensus::SegmentBlockContext context{BlockContext(2)};
    context.block_subsidy = 50;
    context.spend_options.check_no_unspent_output_overwrite = true;
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = context, .transactions = block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(duplicate_outpoint, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = true,
        },
        .previous_median_time_past = 100,
    });

    const auto validation{Consensus::ValidateSegmentSpend(
        blocks,
        external_coins,
        Consensus::ScriptCheckPlanCollection::Skip)};

    BOOST_REQUIRE(!validation);
    BOOST_CHECK_EQUAL(validation.error().reject_reason, "bad-txns-BIP30");
}

BOOST_AUTO_TEST_CASE(segment_spend_validation_rejects_prior_segment_unspent_output_overwrite)
{
    const CTransactionRef duplicate_tx{MakeCoinbase(50)};
    const std::vector<CTransactionRef> first_block{duplicate_tx};
    const std::vector<CTransactionRef> second_block{duplicate_tx};
    Consensus::SegmentBlockContext first_context{BlockContext(2)};
    first_context.block_subsidy = 50;
    Consensus::SegmentBlockContext second_context{BlockContext(3)};
    second_context.block_subsidy = 50;
    second_context.spend_options.check_no_unspent_output_overwrite = true;
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = first_context, .transactions = first_block},
        {.context = second_context, .transactions = second_block},
    };
    const MapBatchView external_coins;

    const auto validation{Consensus::ValidateSegmentSpend(
        blocks,
        external_coins,
        Consensus::ScriptCheckPlanCollection::Skip)};

    BOOST_REQUIRE(!validation);
    BOOST_CHECK_EQUAL(validation.error().reject_reason, "bad-txns-BIP30");
}

BOOST_AUTO_TEST_CASE(segment_spend_validation_accepts_parent_duplicate_spent_by_prior_segment_block)
{
    const CTransactionRef duplicate_tx{MakeCoinbase(50)};
    const COutPoint duplicate_outpoint{duplicate_tx->GetHash(), 0};
    const std::vector<CTransactionRef> first_block{MakeCoinbase(1), MakeSpend(duplicate_outpoint, 49)};
    const std::vector<CTransactionRef> second_block{duplicate_tx};
    Consensus::SegmentBlockContext first_context{BlockContext(200)};
    first_context.block_subsidy = 0;
    Consensus::SegmentBlockContext second_context{BlockContext(201)};
    second_context.block_subsidy = 50;
    second_context.spend_options.check_no_unspent_output_overwrite = true;
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = first_context, .transactions = first_block},
        {.context = second_context, .transactions = second_block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(duplicate_outpoint, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = true,
        },
        .previous_median_time_past = 100,
    });

    const auto validation{Consensus::ValidateSegmentSpend(
        blocks,
        external_coins,
        Consensus::ScriptCheckPlanCollection::Skip)};

    BOOST_REQUIRE(validation);
    BOOST_REQUIRE_EQUAL(validation->block_stages.size(), 2U);
    BOOST_CHECK_EQUAL(validation->block_stages[0].effects.fees, 1);
    BOOST_CHECK_EQUAL(validation->block_stages[1].effects.fees, 0);
}

BOOST_AUTO_TEST_CASE(segment_spend_validation_runs_bulk_join_and_block_stages)
{
    const COutPoint external_prevout{Txid::FromUint256(Uint(280)), 0};
    const CTransactionRef first_spend{MakeSpend(external_prevout, 49)};
    const COutPoint first_spend_output{first_spend->GetHash(), 0};
    const std::vector<CTransactionRef> first_block{MakeCoinbase(1), first_spend};
    const std::vector<CTransactionRef> second_block{MakeCoinbase(1), MakeSpend(first_spend_output, 48)};
    Consensus::SegmentBlockContext first_context{BlockContext(10)};
    first_context.block_subsidy = 0;
    Consensus::SegmentBlockContext second_context{BlockContext(11)};
    second_context.block_subsidy = 0;
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = first_context, .transactions = first_block},
        {.context = second_context, .transactions = second_block},
    };
    MapBatchView external_coins;
    external_coins.coins.emplace(external_prevout, Consensus::SegmentCoinSnapshot{
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{} << OP_TRUE},
            .height = 1,
            .is_coinbase = false,
        },
        .previous_median_time_past = 100,
    });

    auto validation{Consensus::ValidateSegmentSpend(blocks, external_coins, Consensus::ScriptCheckPlanCollection::Skip)};

    BOOST_REQUIRE(validation);
    BOOST_REQUIRE_EQUAL(validation->block_stages.size(), 2U);
    BOOST_CHECK_EQUAL(validation->block_stages[0].effects.fees, 1);
    BOOST_CHECK_EQUAL(validation->block_stages[1].effects.fees, 1);
    BOOST_REQUIRE_EQUAL(validation->summary.spent_outputs.size(), 2U);
    BOOST_CHECK(validation->summary.spent_outputs[1].lookup.lookup.outpoint == first_spend_output);
}

BOOST_AUTO_TEST_CASE(segment_spend_validation_preserves_missing_input_failure_reason)
{
    const COutPoint missing_prevout{Txid::FromUint256(Uint(290)), 0};
    const std::vector<CTransactionRef> block{MakeCoinbase(), MakeSpend(missing_prevout, 49)};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(10), .transactions = block},
    };
    const MapBatchView external_coins;

    const auto validation{Consensus::ValidateSegmentSpend(blocks, external_coins, Consensus::ScriptCheckPlanCollection::Skip)};

    BOOST_REQUIRE(!validation);
    BOOST_CHECK_EQUAL(validation.error().reject_reason, "bad-txns-inputs-missingorspent");
    BOOST_CHECK(validation.error().debug_message.find(block[1]->GetHash().ToString()) != std::string::npos);
}

BOOST_AUTO_TEST_CASE(additive_segment_accumulator_cancels_spent_created_outputs)
{
    const COutPoint outpoint{Txid::FromUint256(Uint(300)), 0};
    const Consensus::CoinSnapshot coin{
        .output = CTxOut{42, CScript{} << OP_TRUE},
        .height = 7,
        .is_coinbase = false,
    };
    const Consensus::SegmentCreatedOutput created{
        .outpoint = outpoint,
        .coin = coin,
        .block_index = 0,
        .transaction_index = 0,
        .output_index = 0,
    };
    const Consensus::SegmentSpentOutputLookupResult spent{
        .lookup = {
            .lookup = {
                .outpoint = outpoint,
                .transaction_index = 1,
                .input_index = 0,
            },
            .block_index = 1,
        },
        .coin = Consensus::SegmentCoinSnapshot{
            .coin = coin,
            .previous_median_time_past = 600,
        },
    };

    Consensus::AdditiveSegmentAccumulator accumulator;
    accumulator.AddCreatedOutput(created);
    accumulator.AddSpentOutput(spent);

    BOOST_CHECK(accumulator.Root().IsNull());
}

BOOST_AUTO_TEST_CASE(segment_finalization_returns_accumulator_artifact)
{
    const CTransactionRef coinbase{MakeCoinbase()};
    const std::vector<CTransactionRef> block{coinbase};
    const std::vector<Consensus::SegmentBlockView> blocks{
        {.context = BlockContext(1), .transactions = block},
    };
    const MapBatchView external_coins;
    const Consensus::SegmentSpentOutputJoin joined{Consensus::JoinSegmentSpentOutputs(blocks, external_coins)};
    const Consensus::SegmentSpendSummary summary{Consensus::SummarizeSegmentSpend(blocks, joined)};

    Consensus::AdditiveSegmentAccumulator accumulator;
    const Consensus::SegmentChainstateArtifact artifact{Consensus::FinalizeSegmentSpend(blocks, summary, accumulator)};

    BOOST_CHECK(artifact.best_block == BlockContext(1).hash);
    BOOST_CHECK_EQUAL(artifact.height, 1);
    BOOST_CHECK_EQUAL(artifact.created_outputs, 1U);
    BOOST_CHECK_EQUAL(artifact.spent_outputs, 0U);
    BOOST_CHECK(!artifact.accumulator_root.IsNull());
}

BOOST_AUTO_TEST_SUITE_END()
