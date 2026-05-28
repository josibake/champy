// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/consensus_fixture.h>

#include <consensus/block_check.h>
#include <consensus/block_commit.h>
#include <consensus/script_checker.h>
#include <consensus/block_consensus_pipeline.h>
#include <chain.h>
#include <coins.h>
#include <script/interpreter.h>
#include <script/verify_flags.h>
#include <test/util/consensus_serialization.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace test::consensus {
namespace {

COutPoint ReadOutPoint(std::string_view value)
{
    const auto separator{value.rfind(':')};
    if (separator == std::string_view::npos) {
        throw std::runtime_error{"outpoint must use <txid>:<vout>"};
    }

    const auto txid{Txid::FromHex(std::string{value.substr(0, separator)})};
    if (!txid) {
        throw std::runtime_error{"outpoint has invalid txid"};
    }

    const auto output_index{ToIntegral<uint32_t>(value.substr(separator + 1))};
    if (!output_index) {
        throw std::runtime_error{"outpoint has invalid output index"};
    }

    return COutPoint{*txid, *output_index};
}

Consensus::BlockConsensusStage ReadConformanceStage(std::string_view value)
{
    if (value == "structural") return Consensus::BlockConsensusStage::Structural;
    if (value == "contextual") return Consensus::BlockConsensusStage::Contextual;
    if (value == "spend") return Consensus::BlockConsensusStage::Spend;
    if (value == "commit") return Consensus::BlockConsensusStage::Commit;
    throw std::runtime_error{"unknown conformance stage"};
}

ConformanceContextualOptions ReadContextualOptions(const UniValue& value)
{
    return ConformanceContextualOptions{
        .difficulty_adjustment_interval = value["difficulty_adjustment_interval"].getInt<int>(),
        .previous_block_time = value["previous_block_time"].getInt<int64_t>(),
        .enforce_timewarp_protection = value["enforce_timewarp_protection"].get_bool(),
        .height_in_coinbase_active = value["height_in_coinbase_active"].get_bool(),
        .der_signature_active = value["der_signature_active"].get_bool(),
        .cltv_active = value["cltv_active"].get_bool(),
        .csv_active = value["csv_active"].get_bool(),
        .segwit_active = value["segwit_active"].get_bool(),
    };
}

Consensus::BlockSpendConsensusOptions ReadSpendOptions(const UniValue& value)
{
    const auto script_flags{value["script_flags"].getInt<int64_t>()};
    if (script_flags < 0) {
        throw std::runtime_error{"script_flags must be non-negative"};
    }

    return Consensus::BlockSpendConsensusOptions{
        .locktime_flags = value["locktime_flags"].getInt<int>(),
        .script_flags = script_verify_flags::from_int(static_cast<script_verify_flags::value_type>(script_flags)),
        .check_no_unspent_output_overwrite = value["check_no_unspent_output_overwrite"].get_bool(),
    };
}

ConformanceCoin ReadCoin(const UniValue& value, int64_t default_previous_median_time_past)
{
    return ConformanceCoin{
        .outpoint = ReadOutPoint(value["outpoint"].get_str()),
        .coin = Consensus::CoinSnapshot{
            .output = ParseExactTxOutHex(value["output"].get_str()),
            .height = value["height"].getInt<int>(),
            .is_coinbase = value["coinbase"].get_bool(),
        },
        .previous_median_time_past = value.exists("previous_median_time_past") ?
            value["previous_median_time_past"].getInt<int64_t>() :
            default_previous_median_time_past,
    };
}

std::string BoolString(bool value)
{
    return value ? "true" : "false";
}

std::string OptionalString(const std::optional<std::string>& value)
{
    return value ? *value : "<none>";
}

ConformanceMismatch Mismatch(std::string field, std::string expected, std::string actual)
{
    return ConformanceMismatch{
        .field = std::move(field),
        .expected = std::move(expected),
        .actual = std::move(actual),
    };
}

ConformanceResult RejectedConformanceResult(const Consensus::BlockConsensusStageError& error)
{
    return ConformanceResult{
        .valid = false,
        .stage = error.stage,
        .reject_reason = error.reject_reason,
    };
}

std::optional<ConformanceResult> RunPreSpendConformanceStages(const ConformanceFixture& fixture)
{
    const auto structural{Consensus::ValidateBlockStructuralStage(
        fixture.block,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true})};
    if (!structural) {
        return RejectedConformanceResult(Consensus::BuildBlockConsensusStageError(
            Consensus::BlockConsensusStage::Structural,
            structural.error()));
    }

    const auto contextual{Consensus::ValidateBlockContextualStage(
        fixture.block,
        BuildBlockContextualConsensusOptions(fixture))};
    if (!contextual) {
        return RejectedConformanceResult(Consensus::BuildBlockConsensusStageError(
            Consensus::BlockConsensusStage::Contextual,
            contextual.error()));
    }

    return std::nullopt;
}

class RecordingBlockCommitSideEffects final : public Consensus::BlockRevertDataWriter, public Consensus::BlockMetadataCommitter {
public:
    [[nodiscard]] Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return {};
    }

    [[nodiscard]] Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return {};
    }
};

class RecordingBlockSpendStateCommitter final : public Consensus::BlockSpendStateCommitter {
public:
    [[nodiscard]] Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return {};
    }
};

} // namespace

ConformanceFixture ReadConformanceFixture(const UniValue& fixture)
{
    ConformanceFixture result{
        .network = fixture["network"].get_str(),
        .validation_time = fixture["validation_time"].getInt<int64_t>(),
        .block_subsidy = fixture["block_subsidy"].getInt<CAmount>(),
        .headers = {},
        .block = ParseExactBlockHex(fixture["block"].get_str()),
        .spend_state = ConformanceSpendState{
            .backend = fixture["spend_state"]["backend"].get_str(),
            .coins = {},
        },
        .spend_context = ConformanceSpendContext{
            .block_height = fixture["spend_context"]["block_height"].getInt<int>(),
            .previous_median_time_past = fixture["spend_context"]["previous_median_time_past"].getInt<int64_t>(),
        },
        .contextual_options = ReadContextualOptions(fixture["contextual_options"]),
        .spend_options = ReadSpendOptions(fixture["spend_options"]),
        .expected = ConformanceExpected{
            .valid = fixture["expected"]["valid"].get_bool(),
            .stage = ReadConformanceStage(fixture["expected"]["stage"].get_str()),
            .reject_reason = fixture["expected"].exists("reject_reason") ? std::make_optional(fixture["expected"]["reject_reason"].get_str()) : std::nullopt,
            .fees = fixture["expected"]["fees"].getInt<int64_t>(),
            .inputs = fixture["expected"]["inputs"].getInt<int>(),
            .sigop_cost = fixture["expected"]["sigop_cost"].getInt<int64_t>(),
        },
    };

    for (const auto& header : fixture["headers"].getValues()) {
        result.headers.push_back(ParseExactBlockHeaderHex(header.get_str()));
    }

    for (const auto& coin : fixture["spend_state"]["coins"].getValues()) {
        result.spend_state.coins.push_back(ReadCoin(coin, result.spend_context.previous_median_time_past));
    }

    return result;
}

Consensus::SnapshotSpendState LoadSnapshotSpendState(const ConformanceFixture& fixture)
{
    const ConformanceSpendState& spend_state{fixture.spend_state};
    if (spend_state.backend != "utxo") {
        throw std::runtime_error{"LoadSnapshotSpendState only supports utxo spend_state fixtures"};
    }

    Consensus::SnapshotSpendState state;
    for (const ConformanceCoin& coin : spend_state.coins) {
        state.AddCoin(coin.outpoint, coin.coin, coin.previous_median_time_past);
    }
    return state;
}

Consensus::BlockConsensusContext BuildBlockConsensusContext(const ConformanceFixture& fixture)
{
    return Consensus::BlockConsensusContext{
        .spend = Consensus::BlockSpendContext{
            .block_height = fixture.spend_context.block_height,
            .previous_median_time_past = fixture.spend_context.previous_median_time_past,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = fixture.block.GetHash()},
        .block_subsidy = fixture.block_subsidy,
    };
}

Consensus::BlockContextualConsensusOptions BuildBlockContextualConsensusOptions(const ConformanceFixture& fixture)
{
    const int64_t locktime_cutoff{
        fixture.contextual_options.csv_active ?
            fixture.spend_context.previous_median_time_past :
            fixture.block.GetBlockTime()};

    return Consensus::BlockContextualConsensusOptions{
        .header = Consensus::BlockContextualHeaderOptions{
            .block_height = fixture.spend_context.block_height,
            .difficulty_adjustment_interval = fixture.contextual_options.difficulty_adjustment_interval,
            .previous_median_time_past = fixture.spend_context.previous_median_time_past,
            .previous_block_time = fixture.contextual_options.previous_block_time,
            .max_block_time = fixture.validation_time + MAX_FUTURE_BLOCK_TIME,
            .enforce_timewarp_protection = fixture.contextual_options.enforce_timewarp_protection,
            .height_in_coinbase_active = fixture.contextual_options.height_in_coinbase_active,
            .der_signature_active = fixture.contextual_options.der_signature_active,
            .cltv_active = fixture.contextual_options.cltv_active,
        },
        .body = Consensus::BlockContextualBodyOptions{
            .transactions = Consensus::BlockContextualTransactionOptions{
                .block_height = fixture.spend_context.block_height,
                .locktime_cutoff = locktime_cutoff,
                .enforce_coinbase_height = fixture.contextual_options.height_in_coinbase_active,
            },
            .expect_witness_commitment = fixture.contextual_options.segwit_active,
        },
    };
}

ConformanceResult RunConformanceSpendAndCommitStages(const ConformanceFixture& fixture, Consensus::BlockSpendBackend& spend_backend)
{
    const Consensus::BlockConsensusContext consensus_context{BuildBlockConsensusContext(fixture)};
    Consensus::DirectBlockScriptChecker script_checker;
    RecordingBlockSpendStateCommitter spend_state_committer;
    RecordingBlockCommitSideEffects side_effects;
    auto workspace{spend_backend.BeginBlockSpend(consensus_context.spend)};
    if (!workspace) {
        return RejectedConformanceResult(Consensus::BuildBlockConsensusStageError(
            Consensus::BlockConsensusStage::Spend,
            workspace.error()));
    }

    Consensus::BlockConsensusPipeline pipeline{fixture.block, consensus_context};
    auto effects{pipeline.ValidateAndCompleteSpendStage(**workspace, script_checker, fixture.spend_options)};
    if (!effects) {
        return RejectedConformanceResult(Consensus::BuildBlockConsensusStageError(
            Consensus::BlockConsensusStage::Spend,
            effects.error()));
    }

    if (const auto commit{Consensus::CommitBlockStageEffects(consensus_context.commit, *effects, side_effects, spend_state_committer, side_effects)}; !commit) {
        return RejectedConformanceResult(commit.error());
    }

    return ConformanceResult{
        .valid = true,
        .stage = Consensus::BlockConsensusStage::Commit,
        .reject_reason = std::nullopt,
        .fees = effects->fees,
        .inputs = effects->inputs,
        .sigop_cost = effects->sigop_cost,
    };
}

ConformanceResult RunInternalConsensusFixture(const ConformanceFixture& fixture)
{
    if (const auto result{RunPreSpendConformanceStages(fixture)}) return *result;

    Consensus::SnapshotSpendState spend_backend{LoadSnapshotSpendState(fixture)};
    return RunConformanceSpendAndCommitStages(fixture, spend_backend);
}

ConformanceResult RunInternalConsensusFixtureStagedApi(const ConformanceFixture& fixture)
{
    const Consensus::BlockConsensusContext consensus_context{BuildBlockConsensusContext(fixture)};
    Consensus::SnapshotSpendState spend_backend{LoadSnapshotSpendState(fixture)};
    Consensus::DirectBlockScriptChecker script_checker;
    RecordingBlockSpendStateCommitter spend_state_committer;
    RecordingBlockCommitSideEffects side_effects;
    auto workspace{spend_backend.BeginBlockSpend(consensus_context.spend)};
    if (!workspace) {
        return RejectedConformanceResult(Consensus::BuildBlockConsensusStageError(
            Consensus::BlockConsensusStage::Spend,
            workspace.error()));
    }

    const auto validation_view{Consensus::BuildBlockPrecommitValidationView(fixture.block)};
    auto effects{Consensus::ValidateAndCommitBlock(
        validation_view,
        Consensus::BlockStructuralConsensusOptions{.check_merkle_root = true},
        BuildBlockContextualConsensusOptions(fixture),
        consensus_context,
        **workspace,
        script_checker,
        fixture.spend_options,
        side_effects,
        spend_state_committer,
        side_effects)};
    if (!effects) {
        return RejectedConformanceResult(effects.error());
    }

    return ConformanceResult{
        .valid = true,
        .stage = Consensus::BlockConsensusStage::Commit,
        .reject_reason = std::nullopt,
        .fees = effects->fees,
        .inputs = effects->inputs,
        .sigop_cost = effects->sigop_cost,
    };
}

ConformanceResult RunCoreSpendStateConsensusFixture(const ConformanceFixture& fixture)
{
    if (const auto result{RunPreSpendConformanceStages(fixture)}) return *result;

    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreConformanceAdapter adapter{coins};
    adapter.LoadSpendState(fixture.spend_state);
    auto spend_backend{adapter.BlockSpendBackend()};
    return RunConformanceSpendAndCommitStages(fixture, spend_backend);
}

std::optional<ConformanceMismatch> CompareConformanceResult(const ConformanceExpected& expected, const ConformanceResult& actual)
{
    if (expected.valid != actual.valid) {
        return Mismatch("valid", BoolString(expected.valid), BoolString(actual.valid));
    }

    if (expected.stage != actual.stage) {
        return Mismatch(
            "stage",
            std::string{Consensus::BlockConsensusStageName(expected.stage)},
            std::string{Consensus::BlockConsensusStageName(actual.stage)});
    }

    if (expected.valid && actual.reject_reason) {
        return Mismatch("reject_reason", "<none>", *actual.reject_reason);
    }

    if (expected.reject_reason) {
        if (expected.reject_reason != actual.reject_reason) {
            return Mismatch("reject_reason", OptionalString(expected.reject_reason), OptionalString(actual.reject_reason));
        }
    }

    if (!expected.valid) {
        return std::nullopt;
    }

    if (expected.fees != actual.fees) {
        return Mismatch("fees", std::to_string(expected.fees), std::to_string(actual.fees));
    }

    if (expected.inputs != actual.inputs) {
        return Mismatch("inputs", std::to_string(expected.inputs), std::to_string(actual.inputs));
    }

    if (expected.sigop_cost != actual.sigop_cost) {
        return Mismatch("sigop_cost", std::to_string(expected.sigop_cost), std::to_string(actual.sigop_cost));
    }

    return std::nullopt;
}

void CoreConformanceAdapter::LoadSpendState(const ConformanceSpendState& spend_state)
{
    if (spend_state.backend != "utxo") {
        throw std::runtime_error{"CoreConformanceAdapter only supports utxo spend_state fixtures"};
    }

    for (const ConformanceCoin& coin : spend_state.coins) {
        Coin core_coin{coin.coin.output, coin.coin.height, coin.coin.is_coinbase};
        m_coins.AddCoin(coin.outpoint, std::move(core_coin), /*possible_overwrite=*/false);
        m_previous_median_time_past_by_outpoint.insert_or_assign(coin.outpoint, coin.previous_median_time_past);
    }
}

Consensus::CoinsViewSpendState CoreConformanceAdapter::SpendView() const
{
    return Consensus::CoinsViewSpendState{m_coins};
}

Consensus::CoinsViewSequenceLockTimeView CoreConformanceAdapter::SequenceLockTimes() const
{
    return Consensus::CoinsViewSequenceLockTimeView{/*previous_median_time_past=*/0, m_previous_median_time_past_by_outpoint};
}

Consensus::CoinsViewBlockSpendBackend CoreConformanceAdapter::BlockSpendBackend()
{
    return Consensus::CoinsViewBlockSpendBackend{m_coins, m_previous_median_time_past_by_outpoint};
}

} // namespace test::consensus
