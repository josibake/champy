// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/snapshot_spend_state.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <string>

namespace {

COutPoint OutPoint(uint32_t output_index)
{
    return COutPoint{Txid::FromUint256(uint256::ONE), output_index};
}

Consensus::CoinSnapshot Coin(CAmount value, int height = 1)
{
    return Consensus::CoinSnapshot{
        .output = CTxOut{value, CScript{} << OP_TRUE},
        .height = height,
        .is_coinbase = false,
    };
}

void CheckRejectReason(const Consensus::BlockSpendResult<void>& result, const std::string& reason)
{
    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, reason);
}

void CheckCommitRejectReason(const Consensus::BlockCommitResult<void>& result, const std::string& reason)
{
    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, reason);
}

Consensus::BlockCommitContext CommitContext(int block_height = 2, int64_t previous_median_time_past = 123)
{
    return Consensus::BlockCommitContext{
        .new_best_block = uint256::ONE,
        .block_height = block_height,
        .previous_median_time_past = previous_median_time_past,
    };
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_snapshot_spend_state_tests)

BOOST_AUTO_TEST_CASE(snapshot_spend_state_returns_loaded_coin)
{
    Consensus::SnapshotSpendState state;
    state.AddCoin(OutPoint(0), Coin(5));

    BOOST_CHECK(state.HaveCoin(OutPoint(0)));
    const auto coin{state.GetCoin(OutPoint(0))};
    BOOST_REQUIRE(coin);
    BOOST_CHECK_EQUAL(coin->output.nValue, 5);
    BOOST_CHECK_EQUAL(coin->height, 1);
}

BOOST_AUTO_TEST_CASE(snapshot_spend_state_exposes_sequence_lock_times)
{
    Consensus::SnapshotSpendState state;
    state.AddCoin(OutPoint(0), Coin(5), /*previous_median_time_past=*/123);

    CBlock block;
    const Consensus::BlockSpendContext context{
        .block_height = 2,
        .previous_median_time_past = 456,
    };
    const auto workspace{state.BeginBlockSpend(context)};

    BOOST_REQUIRE(workspace);
    BOOST_CHECK_EQUAL((*workspace)->SequenceLockTimes().PreviousMedianTimePast(OutPoint(0), /*coin_height=*/1), 123);
    BOOST_CHECK_EQUAL((*workspace)->SequenceLockTimes().PreviousMedianTimePast(OutPoint(1), /*coin_height=*/2), 456);
}

BOOST_AUTO_TEST_CASE(snapshot_spend_state_stages_spends_and_creates)
{
    Consensus::SnapshotSpendState state;
    state.AddCoin(OutPoint(0), Coin(5));
    auto workspace{state.MakeWorkspace()};

    Consensus::TransactionCoinEffects effects;
    effects.spends.push_back({
        .outpoint = OutPoint(0),
        .coin = Coin(5),
    });
    effects.creates.push_back({
        .outpoint = OutPoint(1),
        .coin = Coin(4, 2),
    });

    BOOST_CHECK(workspace.StageTransactionEffectsForIntraBlockView(effects, /*transaction_index=*/1));
    BOOST_CHECK(state.HaveCoin(OutPoint(0)));
    BOOST_CHECK(!workspace.HaveCoin(OutPoint(0)));
    const auto created{workspace.GetCoin(OutPoint(1))};
    BOOST_REQUIRE(created);
    BOOST_CHECK_EQUAL(created->output.nValue, 4);
    BOOST_CHECK_EQUAL(created->height, 2);
}

BOOST_AUTO_TEST_CASE(snapshot_spend_state_rejects_missing_staged_spend)
{
    Consensus::SnapshotSpendState state;
    auto workspace{state.MakeWorkspace()};
    Consensus::TransactionCoinEffects effects;
    effects.spends.push_back({
        .outpoint = OutPoint(0),
        .coin = Coin(5),
    });

    CheckRejectReason(workspace.StageTransactionEffectsForIntraBlockView(effects, /*transaction_index=*/1), "bad-txns-inputs-missingorspent");
}

BOOST_AUTO_TEST_CASE(snapshot_spend_state_rejects_duplicate_create)
{
    Consensus::SnapshotSpendState state;
    state.AddCoin(OutPoint(0), Coin(5));
    auto workspace{state.MakeWorkspace()};

    Consensus::TransactionCoinEffects effects;
    effects.creates.push_back({
        .outpoint = OutPoint(0),
        .coin = Coin(4, 2),
    });

    CheckRejectReason(workspace.StageTransactionEffectsForIntraBlockView(effects, /*transaction_index=*/1), "bad-txns-BIP30");
}

BOOST_AUTO_TEST_CASE(snapshot_spend_state_begins_independent_workspaces)
{
    Consensus::SnapshotSpendState state;
    state.AddCoin(OutPoint(0), Coin(5));

    CBlock block;
    const Consensus::BlockSpendContext context{
        .block_height = 2,
        .previous_median_time_past = 0,
    };
    const auto first{state.BeginBlockSpend(context)};
    const auto second{state.BeginBlockSpend(context)};

    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    BOOST_CHECK((*first)->StagedSpendView().HaveCoin(OutPoint(0)));
    BOOST_CHECK((*second)->StagedSpendView().HaveCoin(OutPoint(0)));

    Consensus::TransactionCoinEffects effects;
    effects.spends.push_back({
        .outpoint = OutPoint(0),
        .coin = Coin(5),
    });
    BOOST_CHECK((*first)->StageTransactionEffectsForIntraBlockView(effects, /*transaction_index=*/1));
    BOOST_CHECK(!(*first)->StagedSpendView().HaveCoin(OutPoint(0)));
    BOOST_CHECK((*second)->StagedSpendView().HaveCoin(OutPoint(0)));
    BOOST_CHECK(state.HaveCoin(OutPoint(0)));
}

BOOST_AUTO_TEST_CASE(snapshot_spend_state_commits_effects_to_parent_state)
{
    Consensus::SnapshotSpendState state;
    state.AddCoin(OutPoint(0), Coin(5), /*previous_median_time_past=*/111);

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.push_back({
        .spends = {{
            .outpoint = OutPoint(0),
            .coin = Coin(5),
        }},
        .creates = {{
            .outpoint = OutPoint(1),
            .coin = Coin(4, 2),
        }},
    });

    BOOST_CHECK(state.CommitSpendState(CommitContext(), effects));
    BOOST_CHECK(!state.HaveCoin(OutPoint(0)));
    const auto created{state.GetCoin(OutPoint(1))};
    BOOST_REQUIRE(created);
    BOOST_CHECK_EQUAL(created->output.nValue, 4);

    const Consensus::BlockSpendContext next_block_context{
        .block_height = 3,
        .previous_median_time_past = 999,
    };
    const auto workspace{state.BeginBlockSpend(next_block_context)};
    BOOST_REQUIRE(workspace);
    BOOST_CHECK_EQUAL((*workspace)->SequenceLockTimes().PreviousMedianTimePast(OutPoint(1), /*coin_height=*/2), 123);
}

BOOST_AUTO_TEST_CASE(snapshot_spend_state_commit_is_atomic_on_error)
{
    Consensus::SnapshotSpendState state;
    state.AddCoin(OutPoint(0), Coin(5));
    state.AddCoin(OutPoint(1), Coin(8));

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.push_back({
        .spends = {{
            .outpoint = OutPoint(0),
            .coin = Coin(5),
        }},
        .creates = {{
            .outpoint = OutPoint(1),
            .coin = Coin(4, 2),
        }},
    });

    CheckCommitRejectReason(state.CommitSpendState(CommitContext(), effects), "snapshot-commit-duplicate-create");
    BOOST_CHECK(state.HaveCoin(OutPoint(0)));
    const auto unchanged{state.GetCoin(OutPoint(1))};
    BOOST_REQUIRE(unchanged);
    BOOST_CHECK_EQUAL(unchanged->output.nValue, 8);
}

BOOST_AUTO_TEST_SUITE_END()
