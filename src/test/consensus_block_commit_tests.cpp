// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_commit.h>
#include <script/script.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace {

class FakeBlockCommitter final : public Consensus::BlockRevertDataWriter, public Consensus::SpendCommitter, public Consensus::BlockMetadataCommitter {
public:
    std::optional<std::string> fail_step;
    Consensus::BlockCommitFailureState fail_state{Consensus::BlockCommitFailureState::Unchanged};
    std::vector<std::string> calls;

    Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Record("revert-data");
    }

    Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Record("spend-state");
    }

    Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Record("metadata");
    }

private:
    Consensus::BlockCommitResult<void> Record(std::string step)
    {
        calls.push_back(step);
        if (fail_step == step) {
            return Consensus::Unexpected<Consensus::BlockCommitError>{Consensus::BlockCommitError{
                .failure_state = fail_state,
                .reject_reason = step + "-failed",
            }};
        }
        return {};
    }
};

struct ObservedCommitCall {
    std::string step;
    Consensus::BlockCommitContext context;
    Consensus::BlockSpendEffects effects;
};

class InspectingBlockCommitter final : public Consensus::BlockRevertDataWriter, public Consensus::SpendCommitter, public Consensus::BlockMetadataCommitter {
public:
    std::vector<ObservedCommitCall> calls;

    Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override
    {
        Record("revert-data", context, effects);
        return {};
    }

    Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override
    {
        Record("spend-state", context, effects);
        return {};
    }

    Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects& effects) override
    {
        Record("metadata", context, effects);
        return {};
    }

private:
    void Record(std::string step, Consensus::BlockCommitContext context, Consensus::BlockSpendEffects effects)
    {
        calls.push_back(ObservedCommitCall{
            .step = std::move(step),
            .context = std::move(context),
            .effects = std::move(effects),
        });
    }
};

void CheckCalls(const std::vector<std::string>& calls, std::initializer_list<std::string> expected)
{
    BOOST_CHECK_EQUAL_COLLECTIONS(calls.begin(), calls.end(), expected.begin(), expected.end());
}

void CheckCommitFailure(
    const Consensus::BlockCommitResult<void>& result,
    const std::string& reason,
    Consensus::BlockCommitFailureState failure_state)
{
    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, reason);
    BOOST_CHECK(result.error().failure_state == failure_state);
}

Consensus::BlockCommitContext CommitContext(int block_height = 0)
{
    return Consensus::BlockCommitContext{
        .new_best_block = uint256::ONE,
        .block_height = block_height,
        .previous_median_time_past = 123,
    };
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_block_commit_tests)

BOOST_AUTO_TEST_CASE(commit_block_effects_writes_revert_data_coins_and_metadata)
{
    FakeBlockCommitter committer;

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    BOOST_CHECK(result);
    CheckCalls(committer.calls, {"revert-data", "spend-state", "metadata"});
}

BOOST_AUTO_TEST_CASE(commit_block_effects_passes_ordered_commit_proof_to_each_boundary)
{
    const COutPoint coinbase_outpoint{Txid::FromUint256(uint256::ONE), 0};
    const uint256 second_txid{uint256{2}};
    const COutPoint spent_outpoint{Txid::FromUint256(second_txid), 1};
    const COutPoint created_outpoint{Txid::FromUint256(second_txid), 0};

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.push_back({
        .spends = {},
        .creates = {{
            .outpoint = coinbase_outpoint,
            .coin = Consensus::CoinSnapshot{
                .output = CTxOut{50, CScript{} << OP_TRUE},
                .height = 42,
                .is_coinbase = true,
            },
        }},
    });
    effects.transaction_effects.push_back({
        .spends = {{
            .outpoint = spent_outpoint,
            .coin = Consensus::CoinSnapshot{
                .output = CTxOut{10, CScript{} << OP_TRUE},
                .height = 7,
                .is_coinbase = false,
            },
        }},
        .creates = {{
            .outpoint = created_outpoint,
            .coin = Consensus::CoinSnapshot{
                .output = CTxOut{9, CScript{} << OP_TRUE},
                .height = 42,
                .is_coinbase = false,
            },
        }},
    });
    effects.fees = 1;
    effects.inputs = 2;
    effects.sigop_cost = 4;

    InspectingBlockCommitter committer;
    BOOST_REQUIRE(Consensus::CommitBlockEffects(CommitContext(/*block_height=*/42), effects, committer, committer, committer));

    BOOST_REQUIRE_EQUAL(committer.calls.size(), 3U);
    BOOST_CHECK_EQUAL(committer.calls[0].step, "revert-data");
    BOOST_CHECK_EQUAL(committer.calls[1].step, "spend-state");
    BOOST_CHECK_EQUAL(committer.calls[2].step, "metadata");

    for (const ObservedCommitCall& call : committer.calls) {
        BOOST_CHECK(call.context.new_best_block == uint256::ONE);
        BOOST_CHECK_EQUAL(call.context.block_height, 42);
        BOOST_CHECK_EQUAL(call.context.previous_median_time_past, 123);
        BOOST_CHECK_EQUAL(call.effects.fees, 1);
        BOOST_CHECK_EQUAL(call.effects.inputs, 2);
        BOOST_CHECK_EQUAL(call.effects.sigop_cost, 4);
        BOOST_REQUIRE_EQUAL(call.effects.transaction_effects.size(), 2U);
        BOOST_REQUIRE_EQUAL(call.effects.transaction_effects[0].creates.size(), 1U);
        BOOST_CHECK(call.effects.transaction_effects[0].creates[0].outpoint == coinbase_outpoint);
        BOOST_REQUIRE_EQUAL(call.effects.transaction_effects[1].spends.size(), 1U);
        BOOST_CHECK(call.effects.transaction_effects[1].spends[0].outpoint == spent_outpoint);
        BOOST_CHECK_EQUAL(call.effects.transaction_effects[1].spends[0].coin.output.nValue, 10);
        BOOST_REQUIRE_EQUAL(call.effects.transaction_effects[1].creates.size(), 1U);
        BOOST_CHECK(call.effects.transaction_effects[1].creates[0].outpoint == created_outpoint);
        BOOST_CHECK_EQUAL(call.effects.transaction_effects[1].creates[0].coin.output.nValue, 9);
    }
}

BOOST_AUTO_TEST_CASE(commit_block_effects_stops_after_revert_data_error)
{
    FakeBlockCommitter committer;
    committer.fail_step = "revert-data";

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    CheckCommitFailure(result, "revert-data-failed", Consensus::BlockCommitFailureState::Unchanged);
    CheckCalls(committer.calls, {"revert-data"});
}

BOOST_AUTO_TEST_CASE(commit_block_effects_preserves_tainted_revert_data_error)
{
    FakeBlockCommitter committer;
    committer.fail_step = "revert-data";
    committer.fail_state = Consensus::BlockCommitFailureState::Tainted;

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    CheckCommitFailure(result, "revert-data-failed", Consensus::BlockCommitFailureState::Tainted);
    CheckCalls(committer.calls, {"revert-data"});
}

BOOST_AUTO_TEST_CASE(commit_block_effects_stops_after_spend_state_error)
{
    FakeBlockCommitter committer;
    committer.fail_step = "spend-state";

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    CheckCommitFailure(result, "spend-state-failed", Consensus::BlockCommitFailureState::Tainted);
    CheckCalls(committer.calls, {"revert-data", "spend-state"});
}

BOOST_AUTO_TEST_CASE(commit_block_effects_returns_metadata_error)
{
    FakeBlockCommitter committer;
    committer.fail_step = "metadata";

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    CheckCommitFailure(result, "metadata-failed", Consensus::BlockCommitFailureState::Tainted);
    CheckCalls(committer.calls, {"revert-data", "spend-state", "metadata"});
}

BOOST_AUTO_TEST_SUITE_END()
