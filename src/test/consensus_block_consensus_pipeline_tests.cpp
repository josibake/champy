// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_consensus_pipeline.h>
#include <consensus/merkle.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace {

CTransactionRef MakeCoinbase(CAmount value)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeSpend(const COutPoint& outpoint, CAmount value)
{
    CMutableTransaction tx;
    tx.vin.emplace_back(outpoint);
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

class FakeSpendView final : public Consensus::SpendStateView {
public:
    bool HaveCoin(const COutPoint& outpoint) const override { return m_outpoint && *m_outpoint == outpoint; }

    std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const override
    {
        if (!HaveCoin(outpoint)) return std::nullopt;
        return m_coin;
    }

    void AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin)
    {
        m_outpoint = outpoint;
        m_coin = coin;
    }

private:
    std::optional<COutPoint> m_outpoint;
    Consensus::CoinSnapshot m_coin;
};

class FakeSequenceLockTimeView final : public Consensus::SequenceLockTimeView {
public:
    int64_t PreviousMedianTimePast(const COutPoint&, int) const override { return 0; }
};

class FakeBlockSpendWorkspace final : public Consensus::BlockSpendWorkspace {
public:
    int stages{0};
    std::optional<unsigned int> fail_stage_index;
    std::vector<unsigned int> staged_indices;

    void AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin) { m_view.AddCoin(outpoint, coin); }

    const Consensus::SpendStateView& StagedSpendView() const override { return m_view; }
    const Consensus::SequenceLockTimeView& SequenceLockTimes() const override { return m_sequence_lock_times; }

    Consensus::BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(const Consensus::TransactionCoinEffects& coin_effects, unsigned int transaction_index) override
    {
        ++stages;
        staged_indices.push_back(transaction_index);
        if (transaction_index == 0) BOOST_CHECK(coin_effects.spends.empty());
        if (fail_stage_index && transaction_index == *fail_stage_index) {
            return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
                .issue = Consensus::BlockConsensusIssue::Consensus,
                .reject_reason = "fake-stage-error",
                .debug_message = "fake stage failure",
            }};
        }
        return {};
    }

private:
    FakeSpendView m_view;
    FakeSequenceLockTimeView m_sequence_lock_times;
};

class NoopScriptChecker final : public Consensus::BlockScriptChecker {
public:
    int checks{0};
    int completions{0};
    std::optional<Consensus::BlockSpendError> complete_error;

    Consensus::BlockSpendResult<void> Check(const Consensus::TransactionScriptCheckPlan&) override
    {
        ++checks;
        return {};
    }

    Consensus::BlockSpendResult<void> Complete() override
    {
        ++completions;
        if (complete_error) return Consensus::Unexpected<Consensus::BlockSpendError>{*complete_error};
        return {};
    }
};

class FakeBlockCommitter final : public Consensus::BlockRevertDataWriter, public Consensus::BlockSpendStateCommitter, public Consensus::BlockMetadataCommitter {
public:
    bool fail_commit{false};
    int commits{0};

    Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override { return {}; }
    Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override { return {}; }

    Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        ++commits;
        if (fail_commit) {
            return Consensus::Unexpected<Consensus::BlockCommitError>{Consensus::BlockCommitError{
                .reject_reason = "metadata-failed",
            }};
        }
        return {};
    }
};

template <typename T>
void CheckRejectReason(const Consensus::BlockSpendResult<T>& result, const std::string& reason)
{
    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, reason);
}

template <typename T>
void CheckBlockRejectReason(const Consensus::BlockCheckResult<T>& result, const std::string& reason)
{
    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, reason);
}

template <typename T>
void CheckStageRejectReason(const Consensus::BlockConsensusStageResult<T>& result, Consensus::BlockConsensusStage stage, const std::string& reason)
{
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().stage == stage);
    BOOST_CHECK_EQUAL(result.error().reject_reason, reason);
}

Consensus::BlockContextualConsensusOptions ContextualOptions(const CBlock& block)
{
    return Consensus::BlockContextualConsensusOptions{
        .header = Consensus::BlockContextualHeaderOptions{
            .block_height = 2,
            .difficulty_adjustment_interval = 2016,
            .previous_median_time_past = 0,
            .previous_block_time = 0,
            .max_block_time = 100,
            .enforce_timewarp_protection = false,
            .height_in_coinbase_active = false,
            .der_signature_active = false,
            .cltv_active = false,
        },
        .body = Consensus::BlockContextualBodyOptions{
            .transactions = Consensus::BlockContextualTransactionOptions{
                .block_height = 2,
                .locktime_cutoff = block.GetBlockTime(),
                .enforce_coinbase_height = false,
            },
            .expect_witness_commitment = false,
        },
    };
}

Consensus::BlockConsensusContext ConsensusContext(CAmount block_subsidy = 50)
{
    return Consensus::BlockConsensusContext{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = block_subsidy,
    };
}

CBlock BlockWithTransactions(std::vector<CTransactionRef> transactions)
{
    CBlock block;
    block.nTime = 1;
    block.vtx = std::move(transactions);
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_block_consensus_pipeline_tests)

BOOST_AUTO_TEST_CASE(consensus_stage_names_match_fixture_contract)
{
    BOOST_CHECK_EQUAL(Consensus::BlockConsensusStageName(Consensus::BlockConsensusStage::Structural), "structural");
    BOOST_CHECK_EQUAL(Consensus::BlockConsensusStageName(Consensus::BlockConsensusStage::Contextual), "contextual");
    BOOST_CHECK_EQUAL(Consensus::BlockConsensusStageName(Consensus::BlockConsensusStage::Spend), "spend");
    BOOST_CHECK_EQUAL(Consensus::BlockConsensusStageName(Consensus::BlockConsensusStage::Commit), "commit");
}

BOOST_AUTO_TEST_CASE(consensus_stage_error_preserves_rule_diagnostics)
{
    const Consensus::BlockCheckError check_error{
        .issue = Consensus::BlockConsensusIssue::InvalidHeader,
        .reject_reason = "time-too-new",
        .debug_message = "block timestamp too far in the future",
    };
    const auto contextual{Consensus::BuildBlockConsensusStageError(
        Consensus::BlockConsensusStage::Contextual,
        check_error)};
    BOOST_CHECK(contextual.stage == Consensus::BlockConsensusStage::Contextual);
    BOOST_REQUIRE(contextual.issue);
    BOOST_CHECK(*contextual.issue == Consensus::BlockConsensusIssue::InvalidHeader);
    BOOST_CHECK_EQUAL(contextual.reject_reason, "time-too-new");
    BOOST_CHECK_EQUAL(contextual.debug_message, "block timestamp too far in the future");

    const Consensus::BlockSpendError spend_error{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = "bad-txns-inputs-missingorspent",
        .debug_message = "CheckTxInputs: inputs missing/spent",
    };
    const auto spend{Consensus::BuildBlockConsensusStageError(
        Consensus::BlockConsensusStage::Spend,
        spend_error)};
    BOOST_CHECK(spend.stage == Consensus::BlockConsensusStage::Spend);
    BOOST_REQUIRE(spend.issue);
    BOOST_CHECK(*spend.issue == Consensus::BlockConsensusIssue::Consensus);
    BOOST_CHECK_EQUAL(spend.reject_reason, "bad-txns-inputs-missingorspent");
    BOOST_CHECK_EQUAL(spend.debug_message, "CheckTxInputs: inputs missing/spent");

    const Consensus::BlockCommitError commit_error{
        .reject_reason = "undo-failed",
    };
    const auto commit{Consensus::BuildBlockConsensusStageError(
        Consensus::BlockConsensusStage::Commit,
        commit_error)};
    BOOST_CHECK(commit.stage == Consensus::BlockConsensusStage::Commit);
    BOOST_CHECK(!commit.issue);
    BOOST_CHECK_EQUAL(commit.reject_reason, "undo-failed");
}

BOOST_AUTO_TEST_CASE(commit_stage_effects_reports_commit_failures)
{
    const Consensus::BlockCommitContext commit_context{.new_best_block = uint256::ONE};
    Consensus::BlockSpendEffects effects;
    FakeBlockCommitter committer;

    BOOST_CHECK(Consensus::CommitBlockStageEffects(commit_context, effects, committer, committer, committer));
    BOOST_CHECK_EQUAL(committer.commits, 1);

    committer.fail_commit = true;
    const auto result{Consensus::CommitBlockStageEffects(commit_context, effects, committer, committer, committer)};
    CheckStageRejectReason(result, Consensus::BlockConsensusStage::Commit, "metadata-failed");
}

BOOST_AUTO_TEST_CASE(validate_and_commit_stages_return_spend_effects)
{
    const Consensus::BlockConsensusContext context{ConsensusContext()};
    const CBlock block{BlockWithTransactions({MakeCoinbase(50)})};
    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    FakeBlockCommitter committer;
    const auto effects{Consensus::ValidateAndCommitBlockStages(
        block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true},
        ContextualOptions(block),
        context,
        spend_state,
        script_checker,
        Consensus::BlockSpendConsensusOptions{},
        committer,
        committer,
        committer)};

    BOOST_REQUIRE(effects);
    BOOST_CHECK_EQUAL(effects->inputs, 1);
    BOOST_CHECK_EQUAL(committer.commits, 1);
}

BOOST_AUTO_TEST_CASE(validate_and_commit_stages_report_commit_failure_stage)
{
    const Consensus::BlockConsensusContext context{ConsensusContext()};
    const CBlock block{BlockWithTransactions({MakeCoinbase(50)})};
    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    FakeBlockCommitter committer;
    committer.fail_commit = true;
    const auto effects{Consensus::ValidateAndCommitBlockStages(
        block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true},
        ContextualOptions(block),
        context,
        spend_state,
        script_checker,
        Consensus::BlockSpendConsensusOptions{},
        committer,
        committer,
        committer)};

    CheckStageRejectReason(effects, Consensus::BlockConsensusStage::Commit, "metadata-failed");
}

BOOST_AUTO_TEST_CASE(pipeline_builds_consensus_context_from_header_context)
{
    const Consensus::BlockHeaderContext headers{42, 1000, 900};
    const uint256 new_best_block{uint256::ONE};

    const auto context{Consensus::BuildBlockConsensusContext(headers, new_best_block, /*block_subsidy=*/50)};

    BOOST_CHECK_EQUAL(context.spend.block_height, 42);
    BOOST_CHECK_EQUAL(context.spend.previous_median_time_past, 1000);
    BOOST_CHECK(context.commit.new_best_block == new_best_block);
    BOOST_CHECK_EQUAL(context.commit.block_height, 42);
    BOOST_CHECK_EQUAL(context.commit.previous_median_time_past, 1000);
    BOOST_CHECK_EQUAL(context.block_subsidy, 50);
}

BOOST_AUTO_TEST_CASE(pipeline_builds_contextual_options_from_header_context)
{
    CBlock block;
    block.nTime = 400;

    const Consensus::BlockHeaderContext headers{
        42,
        300,
        200,
        Consensus::BlockDeploymentContext{
            .height_in_coinbase_active = true,
            .csv_active = true,
            .segwit_active = true,
        },
    };

    Consensus::Params params{};
    params.nPowTargetTimespan = 1209600;
    params.nPowTargetSpacing = 600;
    params.enforce_BIP94 = true;

    const auto options{Consensus::BuildBlockContextualConsensusOptions(
        block,
        headers,
        params,
        /*max_block_time=*/500)};

    BOOST_CHECK_EQUAL(options.header.block_height, 42);
    BOOST_CHECK_EQUAL(options.header.difficulty_adjustment_interval, 2016);
    BOOST_CHECK_EQUAL(options.header.previous_median_time_past, 300);
    BOOST_CHECK_EQUAL(options.header.previous_block_time, 200);
    BOOST_CHECK_EQUAL(options.header.max_block_time, 500);
    BOOST_CHECK(options.header.enforce_timewarp_protection);
    BOOST_CHECK(options.header.height_in_coinbase_active);
    BOOST_CHECK(!options.header.der_signature_active);
    BOOST_CHECK(!options.header.cltv_active);
    BOOST_CHECK_EQUAL(options.body.transactions.block_height, 42);
    BOOST_CHECK_EQUAL(options.body.transactions.locktime_cutoff, 300);
    BOOST_CHECK(options.body.transactions.enforce_coinbase_height);
    BOOST_CHECK(options.body.expect_witness_commitment);
}

BOOST_AUTO_TEST_CASE(structural_stage_validates_merkle_and_body)
{
    CBlock block;
    block.vtx = {MakeCoinbase(50)};
    block.hashMerkleRoot = BlockMerkleRoot(block);

    BOOST_CHECK(Consensus::ValidateBlockStructuralStage(
        block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true}));

    block.hashMerkleRoot = uint256::ONE;
    CheckBlockRejectReason(
        Consensus::ValidateBlockStructuralStage(
            block,
            Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true}),
        "bad-txnmrklroot");
    BOOST_CHECK(Consensus::ValidateBlockStructuralStage(
        block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = false}));
}

BOOST_AUTO_TEST_CASE(contextual_stage_validates_header_witness_and_transactions)
{
    CBlock block;
    block.nTime = 1;
    block.vtx = {MakeCoinbase(50)};

    BOOST_CHECK(Consensus::ValidateBlockContextualStage(block, ContextualOptions(block)));

    auto options{ContextualOptions(block)};
    options.header.previous_median_time_past = 1;
    CheckBlockRejectReason(Consensus::ValidateBlockContextualStage(block, options), "time-too-old");
}

BOOST_AUTO_TEST_CASE(contextual_body_stage_validates_body_context_without_header_rules)
{
    CBlock block;
    block.nTime = 1;
    block.vtx = {MakeCoinbase(50)};

    auto options{ContextualOptions(block)};
    options.header.previous_median_time_past = 1;

    BOOST_CHECK(Consensus::ValidateBlockContextualBodyStage(block, options.body, "test"));
    CheckBlockRejectReason(Consensus::ValidateBlockContextualStage(block, options), "time-too-old");
}

BOOST_AUTO_TEST_CASE(contextual_body_stage_preserves_transaction_failure_order)
{
    CBlock block;
    block.nTime = 1;
    block.vtx = {MakeCoinbase(50)};

    CMutableTransaction coinbase{*block.vtx[0]};
    coinbase.vin[0].scriptWitness.stack.push_back({});
    block.vtx[0] = MakeTransactionRef(std::move(coinbase));

    auto options{ContextualOptions(block).body};
    options.transactions.enforce_coinbase_height = true;
    options.expect_witness_commitment = false;

    CheckBlockRejectReason(
        Consensus::ValidateBlockContextualBodyStage(block, options, "test"),
        "bad-cb-height");
}

BOOST_AUTO_TEST_CASE(validation_input_view_owns_matching_block_facts)
{
    const Consensus::BlockConsensusContext context{ConsensusContext()};
    const CBlock block{BlockWithTransactions({MakeCoinbase(50)})};
    const Consensus::BlockPrecommitValidationView input{Consensus::BuildBlockPrecommitValidationView(block)};

    BOOST_CHECK_EQUAL(input.Transactions().size(), block.vtx.size());
    BOOST_CHECK_EQUAL(input.Facts().structure.transaction_count, block.vtx.size());

    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    const auto effects{Consensus::ValidateBlockPrecommitStages(
        input,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true},
        ContextualOptions(block),
        context,
        spend_state,
        script_checker,
        Consensus::BlockSpendConsensusOptions{})};

    BOOST_REQUIRE(effects);
    BOOST_CHECK_EQUAL(effects->inputs, 1);
    BOOST_CHECK_EQUAL(script_checker.completions, 1);
}

BOOST_AUTO_TEST_CASE(precommit_stages_return_spend_effects)
{
    const Consensus::BlockConsensusContext context{ConsensusContext()};
    const CBlock block{BlockWithTransactions({MakeCoinbase(50)})};

    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    const auto effects{Consensus::ValidateBlockPrecommit(
        block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true},
        ContextualOptions(block),
        context,
        spend_state,
        script_checker,
        Consensus::BlockSpendConsensusOptions{})};

    BOOST_REQUIRE(effects);
    BOOST_CHECK_EQUAL(effects->inputs, 1);
    BOOST_CHECK_EQUAL(script_checker.completions, 1);
}

BOOST_AUTO_TEST_CASE(precommit_stages_report_contextual_failure_stage)
{
    const Consensus::BlockConsensusContext context{ConsensusContext()};
    const CBlock block{BlockWithTransactions({MakeCoinbase(50)})};

    auto contextual_options{ContextualOptions(block)};
    contextual_options.header.previous_median_time_past = 1;
    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    const auto effects{Consensus::ValidateBlockPrecommitStages(
        block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true},
        contextual_options,
        context,
        spend_state,
        script_checker,
        Consensus::BlockSpendConsensusOptions{})};

    CheckStageRejectReason(effects, Consensus::BlockConsensusStage::Contextual, "time-too-old");
}

BOOST_AUTO_TEST_CASE(precommit_stages_report_spend_failure_stage)
{
    const Consensus::BlockConsensusContext context{ConsensusContext()};

    const COutPoint spent_outpoint{Txid::FromUint256(uint256::ONE), 0};
    const CBlock block{BlockWithTransactions({MakeCoinbase(50), MakeSpend(spent_outpoint, 49)})};

    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    const auto effects{Consensus::ValidateBlockPrecommitStages(
        block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true},
        ContextualOptions(block),
        context,
        spend_state,
        script_checker,
        Consensus::BlockSpendConsensusOptions{})};

    CheckStageRejectReason(effects, Consensus::BlockConsensusStage::Spend, "bad-txns-inputs-missingorspent");
}

BOOST_AUTO_TEST_CASE(pipeline_validates_spend_with_explicit_context)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    CBlock block;
    block.vtx = {MakeCoinbase(50)};

    Consensus::BlockConsensusPipeline pipeline{block, context};
    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    const auto effects{pipeline.ValidateAndStageSpend(spend_state, script_checker, Consensus::BlockSpendConsensusOptions{})};

    BOOST_REQUIRE(effects);
    BOOST_CHECK_EQUAL(effects->inputs, 1);
    BOOST_CHECK_EQUAL(spend_state.stages, 1);
    BOOST_CHECK_EQUAL(script_checker.checks, 0);
    BOOST_CHECK(pipeline.CheckCoinbaseReward(*effects));
}

BOOST_AUTO_TEST_CASE(pipeline_returns_coinbase_reward_diagnostics)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    CBlock block;
    block.vtx = {MakeCoinbase(51)};

    Consensus::BlockConsensusPipeline pipeline{block, context};
    Consensus::BlockSpendEffects effects;
    effects.fees = 0;

    CheckRejectReason(pipeline.CheckCoinbaseReward(effects), "bad-cb-amount");
}

BOOST_AUTO_TEST_CASE(pipeline_applies_fee_to_coinbase_reward_limit)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    CBlock block;
    block.vtx = {MakeCoinbase(60)};

    Consensus::BlockConsensusPipeline pipeline{block, context};
    Consensus::BlockSpendEffects effects;
    effects.fees = 10;

    BOOST_CHECK(pipeline.CheckCoinbaseReward(effects));
}

BOOST_AUTO_TEST_CASE(pipeline_returns_script_completion_diagnostics)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    CBlock block;
    block.vtx = {MakeCoinbase(50)};

    Consensus::BlockConsensusPipeline pipeline{block, context};
    NoopScriptChecker script_checker;
    script_checker.complete_error = Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = "fake-script-complete",
        .debug_message = "fake script completion failure",
    };

    CheckRejectReason(pipeline.CompleteScriptChecks(script_checker), "fake-script-complete");
    BOOST_CHECK_EQUAL(script_checker.completions, 1);
}

BOOST_AUTO_TEST_CASE(pipeline_validates_full_spend_stage)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    CBlock block;
    block.vtx = {MakeCoinbase(50)};

    Consensus::BlockConsensusPipeline pipeline{block, context};
    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    const auto effects{pipeline.ValidateAndCompleteSpendStage(spend_state, script_checker, Consensus::BlockSpendConsensusOptions{})};

    BOOST_REQUIRE(effects);
    BOOST_CHECK_EQUAL(effects->inputs, 1);
    BOOST_CHECK_EQUAL(script_checker.completions, 1);
}

BOOST_AUTO_TEST_CASE(pipeline_split_spend_completion_matches_combined_stage)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    const COutPoint spent_outpoint{Txid::FromUint256(uint256::ONE), 0};
    CBlock block;
    block.vtx = {MakeCoinbase(51), MakeSpend(spent_outpoint, 49)};

    Consensus::BlockConsensusPipeline pipeline{block, context};

    FakeBlockSpendWorkspace combined_spend_state;
    combined_spend_state.AddCoin(spent_outpoint, Consensus::CoinSnapshot{
        .output = CTxOut{50, CScript{} << OP_TRUE},
        .height = 1,
        .is_coinbase = false,
    });
    NoopScriptChecker combined_script_checker;
    const auto combined{pipeline.ValidateAndCompleteSpendStage(combined_spend_state, combined_script_checker, Consensus::BlockSpendConsensusOptions{})};
    BOOST_REQUIRE(combined);

    FakeBlockSpendWorkspace split_spend_state;
    split_spend_state.AddCoin(spent_outpoint, Consensus::CoinSnapshot{
        .output = CTxOut{50, CScript{} << OP_TRUE},
        .height = 1,
        .is_coinbase = false,
    });
    NoopScriptChecker split_script_checker;
    auto split{pipeline.ValidateAndStageSpend(split_spend_state, split_script_checker, Consensus::BlockSpendConsensusOptions{})};
    BOOST_REQUIRE(split);
    BOOST_CHECK_EQUAL(split_script_checker.completions, 0);

    split = pipeline.CompleteSpendStage(std::move(split), split_script_checker);
    BOOST_REQUIRE(split);

    BOOST_CHECK_EQUAL(split->fees, combined->fees);
    BOOST_CHECK_EQUAL(split->inputs, combined->inputs);
    BOOST_CHECK_EQUAL(split->sigop_cost, combined->sigop_cost);
    BOOST_CHECK_EQUAL(split->transaction_effects.size(), combined->transaction_effects.size());
    BOOST_CHECK_EQUAL(split_script_checker.checks, combined_script_checker.checks);
    BOOST_CHECK_EQUAL(split_script_checker.completions, combined_script_checker.completions);
}

BOOST_AUTO_TEST_CASE(pipeline_completes_scripts_after_full_spend_stage_failure)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    const COutPoint spent_outpoint{Txid::FromUint256(uint256::ONE), 0};
    CBlock block;
    block.vtx = {MakeCoinbase(50), MakeSpend(spent_outpoint, 49)};

    Consensus::BlockConsensusPipeline pipeline{block, context};
    FakeBlockSpendWorkspace spend_state;
    spend_state.AddCoin(spent_outpoint, Consensus::CoinSnapshot{
        .output = CTxOut{50, CScript{} << OP_TRUE},
        .height = 1,
        .is_coinbase = false,
    });
    spend_state.fail_stage_index = 1;
    NoopScriptChecker script_checker;
    script_checker.complete_error = Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = "fake-script-complete",
        .debug_message = "fake script completion failure",
    };
    const auto effects{pipeline.ValidateAndCompleteSpendStage(spend_state, script_checker, Consensus::BlockSpendConsensusOptions{})};

    CheckRejectReason(effects, "fake-stage-error");
    BOOST_CHECK_EQUAL(script_checker.checks, 1);
    BOOST_CHECK_EQUAL(script_checker.completions, 1);
}

BOOST_AUTO_TEST_CASE(pipeline_preserves_reward_failure_before_script_completion_failure)
{
    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 2,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };

    CBlock block;
    block.vtx = {MakeCoinbase(51)};

    Consensus::BlockConsensusPipeline pipeline{block, context};
    FakeBlockSpendWorkspace spend_state;
    NoopScriptChecker script_checker;
    script_checker.complete_error = Consensus::BlockSpendError{
        .issue = Consensus::BlockConsensusIssue::Consensus,
        .reject_reason = "fake-script-complete",
        .debug_message = "fake script completion failure",
    };
    auto effects{pipeline.ValidateAndStageSpend(spend_state, script_checker, Consensus::BlockSpendConsensusOptions{})};
    effects = pipeline.CompleteSpendStage(std::move(effects), script_checker);

    CheckRejectReason(effects, "bad-cb-amount");
    BOOST_CHECK_EQUAL(script_checker.completions, 1);
}

BOOST_AUTO_TEST_SUITE_END()
