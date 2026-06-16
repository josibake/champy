// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/consensus_conformance.json.h>
#include <test/util/consensus_fixture.h>
#include <test/util/json.h>

#include <coins.h>
#include <consensus/merkle.h>
#include <script/script.h>
#include <univalue.h>

#include <boost/test/unit_test.hpp>

#include <optional>
#include <string>

namespace {

std::string OptionalString(const std::optional<std::string>& value)
{
    return value ? *value : "<none>";
}

void CheckConformanceResultsEqual(
    const test::consensus::ConformanceResult& expected,
    const test::consensus::ConformanceResult& actual)
{
    BOOST_CHECK_EQUAL(actual.valid, expected.valid);
    BOOST_CHECK_EQUAL(
        std::string{Consensus::BlockConsensusStageName(actual.stage)},
        std::string{Consensus::BlockConsensusStageName(expected.stage)});
    BOOST_CHECK_EQUAL(OptionalString(actual.reject_reason), OptionalString(expected.reject_reason));
    BOOST_CHECK_EQUAL(actual.fees, expected.fees);
    BOOST_CHECK_EQUAL(actual.inputs, expected.inputs);
    BOOST_CHECK_EQUAL(actual.sigop_cost, expected.sigop_cost);
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_conformance_tests)

BOOST_AUTO_TEST_CASE(conformance_fixture_decodes_portable_inputs)
{
    const UniValue fixtures{read_json(json_tests::consensus_conformance)};
    const auto fixture{test::consensus::ReadConformanceFixture(fixtures[0])};

    BOOST_CHECK_EQUAL(fixture.network, "regtest");
    BOOST_CHECK_EQUAL(fixture.validation_time, 1710000000);
    BOOST_CHECK_EQUAL(fixture.block_subsidy, 50);
    BOOST_CHECK_EQUAL(fixture.expected.valid, true);
    BOOST_CHECK(fixture.expected.stage == Consensus::BlockConsensusStage::Commit);
    BOOST_CHECK(!fixture.expected.reject_reason);
    BOOST_CHECK_EQUAL(fixture.expected.fees, 0);
    BOOST_CHECK_EQUAL(fixture.expected.inputs, 1);
    BOOST_CHECK_EQUAL(fixture.expected.sigop_cost, 0);
    BOOST_CHECK_EQUAL(fixture.spend_context.block_height, 2);
    BOOST_CHECK_EQUAL(fixture.spend_context.previous_median_time_past, 0);
    BOOST_CHECK_EQUAL(fixture.contextual_options.difficulty_adjustment_interval, 2016);
    BOOST_CHECK_EQUAL(fixture.contextual_options.previous_block_time, 0);
    BOOST_CHECK(!fixture.contextual_options.enforce_timewarp_protection);
    BOOST_CHECK(!fixture.contextual_options.height_in_coinbase_active);
    BOOST_CHECK(!fixture.contextual_options.der_signature_active);
    BOOST_CHECK(!fixture.contextual_options.cltv_active);
    BOOST_CHECK(!fixture.contextual_options.csv_active);
    BOOST_CHECK(!fixture.contextual_options.segwit_active);
    BOOST_CHECK_EQUAL(fixture.spend_options.locktime_flags, 0);
    BOOST_CHECK_EQUAL(fixture.spend_options.script_flags.as_int(), 0U);
    BOOST_CHECK(!fixture.spend_options.check_no_unspent_output_overwrite);
    BOOST_CHECK_EQUAL(fixture.headers.size(), 0U);
    BOOST_REQUIRE_EQUAL(fixture.block.vtx.size(), 1U);
    BOOST_CHECK_EQUAL(fixture.block.hashMerkleRoot.ToString(), fixture.block.vtx[0]->GetHash().ToString());
    BOOST_CHECK_EQUAL(BlockMerkleRoot(fixture.block).ToString(), fixture.block.hashMerkleRoot.ToString());
    BOOST_CHECK_EQUAL(fixture.spend_state.coins.size(), 0U);
    BOOST_CHECK_EQUAL(fixture.spend_state.backend, "utxo");
}

BOOST_AUTO_TEST_CASE(core_conformance_adapter_loads_utxo_spend_state)
{
    const COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    const test::consensus::ConformanceSpendState fixture_spend_state{
        .backend = "utxo",
        .coins = {
            test::consensus::ConformanceCoin{
                .outpoint = outpoint,
                .coin = Consensus::CoinSnapshot{
                    .output = CTxOut{50, CScript{} << OP_TRUE},
                    .height = 1,
                    .is_coinbase = true,
                },
                .previous_median_time_past = 123,
            },
        },
    };

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    test::consensus::CoreConformanceAdapter adapter{coins};
    adapter.LoadSpendState(fixture_spend_state);
    const auto spend_view{adapter.SpendView()};
    BOOST_CHECK(spend_view.HaveCoin(outpoint));
    const auto coin{spend_view.GetCoin(outpoint)};
    BOOST_REQUIRE(coin);
    BOOST_CHECK_EQUAL(coin->height, 1);
    const auto sequence_lock_times{adapter.SequenceLockTimes()};
    BOOST_CHECK_EQUAL(sequence_lock_times.PreviousMedianTimePast(outpoint, coin->height), 123);
    BOOST_CHECK(coins.HaveCoin(outpoint));
}

BOOST_AUTO_TEST_CASE(conformance_fixture_decodes_coin_sequence_lock_context)
{
    const UniValue fixtures{read_json(json_tests::consensus_conformance)};
    const auto fixture{test::consensus::ReadConformanceFixture(fixtures[3])};

    BOOST_REQUIRE_EQUAL(fixture.spend_state.coins.size(), 1U);
    BOOST_CHECK_EQUAL(fixture.spend_state.coins[0].previous_median_time_past, 0);
}

BOOST_AUTO_TEST_CASE(internal_consensus_adapter_validates_orchestrated_stages)
{
    const UniValue fixtures{read_json(json_tests::consensus_conformance)};
    for (const auto& fixture_value : fixtures.getValues()) {
        const auto fixture{test::consensus::ReadConformanceFixture(fixture_value)};
        const auto result{test::consensus::RunInternalConsensusFixture(fixture)};

        if (const auto mismatch{test::consensus::CompareConformanceResult(fixture.expected, result)}) {
            BOOST_ERROR(mismatch->field << " mismatch: expected " << mismatch->expected << ", got " << mismatch->actual);
        }
    }
}

BOOST_AUTO_TEST_CASE(internal_consensus_staged_api_validates_fixtures)
{
    const UniValue fixtures{read_json(json_tests::consensus_conformance)};
    for (const auto& fixture_value : fixtures.getValues()) {
        const auto fixture{test::consensus::ReadConformanceFixture(fixture_value)};
        const auto result{test::consensus::RunInternalConsensusFixtureStagedApi(fixture)};

        if (const auto mismatch{test::consensus::CompareConformanceResult(fixture.expected, result)}) {
            BOOST_ERROR(mismatch->field << " mismatch: expected " << mismatch->expected << ", got " << mismatch->actual);
        }
    }
}

BOOST_AUTO_TEST_CASE(core_spend_state_adapter_validates_orchestrated_stages)
{
    const UniValue fixtures{read_json(json_tests::consensus_conformance)};
    for (const auto& fixture_value : fixtures.getValues()) {
        const auto fixture{test::consensus::ReadConformanceFixture(fixture_value)};
        const auto result{test::consensus::RunCoreSpendStateConsensusFixture(fixture)};

        if (const auto mismatch{test::consensus::CompareConformanceResult(fixture.expected, result)}) {
            BOOST_ERROR(mismatch->field << " mismatch: expected " << mismatch->expected << ", got " << mismatch->actual);
        }
    }
}

BOOST_AUTO_TEST_CASE(spend_state_backends_agree_on_conformance_fixtures)
{
    const UniValue fixtures{read_json(json_tests::consensus_conformance)};
    for (const auto& fixture_value : fixtures.getValues()) {
        const auto fixture{test::consensus::ReadConformanceFixture(fixture_value)};

        const auto snapshot_result{test::consensus::RunInternalConsensusFixture(fixture)};
        const auto core_result{test::consensus::RunCoreSpendStateConsensusFixture(fixture)};

        CheckConformanceResultsEqual(snapshot_result, core_result);
    }
}

BOOST_AUTO_TEST_CASE(conformance_reports_pre_spend_failure_before_loading_backend)
{
    const UniValue fixtures{read_json(json_tests::consensus_conformance)};
    auto fixture{test::consensus::ReadConformanceFixture(fixtures[5])};
    fixture.spend_state.backend = "unsupported";

    const auto internal_result{test::consensus::RunInternalConsensusFixture(fixture)};
    BOOST_CHECK(!internal_result.valid);
    BOOST_CHECK(internal_result.stage == Consensus::BlockConsensusStage::Structural);
    BOOST_REQUIRE(internal_result.reject_reason);
    BOOST_CHECK_EQUAL(*internal_result.reject_reason, "bad-txnmrklroot");

    const auto core_result{test::consensus::RunCoreSpendStateConsensusFixture(fixture)};
    BOOST_CHECK(!core_result.valid);
    BOOST_CHECK(core_result.stage == Consensus::BlockConsensusStage::Structural);
    BOOST_REQUIRE(core_result.reject_reason);
    BOOST_CHECK_EQUAL(*core_result.reject_reason, "bad-txnmrklroot");
}

BOOST_AUTO_TEST_CASE(conformance_result_comparison_uses_portable_contract)
{
    const test::consensus::ConformanceExpected expected{
        .valid = true,
        .stage = Consensus::BlockConsensusStage::Commit,
        .reject_reason = std::nullopt,
        .fees = 10,
        .inputs = 2,
        .sigop_cost = 3,
    };

    const test::consensus::ConformanceResult matching{
        .valid = true,
        .stage = Consensus::BlockConsensusStage::Commit,
        .reject_reason = std::nullopt,
        .fees = 10,
        .inputs = 2,
        .sigop_cost = 3,
    };
    BOOST_CHECK(!test::consensus::CompareConformanceResult(expected, matching));

    auto mismatch{test::consensus::CompareConformanceResult(
        expected,
        test::consensus::ConformanceResult{
            .valid = false,
            .stage = Consensus::BlockConsensusStage::Spend,
            .reject_reason = std::nullopt,
        })};
    BOOST_REQUIRE(mismatch);
    BOOST_CHECK_EQUAL(mismatch->field, "valid");

    mismatch = test::consensus::CompareConformanceResult(
        expected,
        test::consensus::ConformanceResult{
            .valid = true,
            .stage = Consensus::BlockConsensusStage::Spend,
            .reject_reason = std::nullopt,
        });
    BOOST_REQUIRE(mismatch);
    BOOST_CHECK_EQUAL(mismatch->field, "stage");

    const test::consensus::ConformanceExpected invalid_expected{
        .valid = false,
        .stage = Consensus::BlockConsensusStage::Spend,
        .reject_reason = "bad-txns-inputs-missingorspent",
    };
    mismatch = test::consensus::CompareConformanceResult(
        invalid_expected,
        test::consensus::ConformanceResult{
            .valid = false,
            .stage = Consensus::BlockConsensusStage::Spend,
            .reject_reason = "different",
        });
    BOOST_REQUIRE(mismatch);
    BOOST_CHECK_EQUAL(mismatch->field, "reject_reason");

    mismatch = test::consensus::CompareConformanceResult(
        expected,
        test::consensus::ConformanceResult{
            .valid = true,
            .stage = Consensus::BlockConsensusStage::Commit,
            .reject_reason = std::nullopt,
            .fees = 11,
            .inputs = 2,
            .sigop_cost = 3,
        });
    BOOST_REQUIRE(mismatch);
    BOOST_CHECK_EQUAL(mismatch->field, "fees");
}

BOOST_AUTO_TEST_SUITE_END()
