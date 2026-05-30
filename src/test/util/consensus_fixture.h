// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_CONSENSUS_FIXTURE_H
#define BITCOIN_TEST_UTIL_CONSENSUS_FIXTURE_H

#include <consensus/amount.h>
#include <validation/coins_view_spend_state.h>
#include <consensus/snapshot_spend_state.h>
#include <consensus/spend_state.h>
#include <consensus/block_consensus_pipeline.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

class UniValue;
class CCoinsViewCache;

namespace test::consensus {

struct ConformanceCoin {
    COutPoint outpoint;
    Consensus::CoinSnapshot coin;
    int64_t previous_median_time_past{0};
};

struct ConformanceSpendState {
    std::string backend;
    std::vector<ConformanceCoin> coins;
};

struct ConformanceSpendContext {
    int block_height{0};
    int64_t previous_median_time_past{0};
};

struct ConformanceContextualOptions {
    int difficulty_adjustment_interval{0};
    int64_t previous_block_time{0};
    bool enforce_timewarp_protection{false};
    bool height_in_coinbase_active{false};
    bool der_signature_active{false};
    bool cltv_active{false};
    bool csv_active{false};
    bool segwit_active{false};
};

struct ConformanceExpected {
    bool valid{false};
    Consensus::BlockConsensusStage stage{Consensus::BlockConsensusStage::Structural};
    std::optional<std::string> reject_reason;
    int64_t fees{0};
    int inputs{0};
    int64_t sigop_cost{0};
};

struct ConformanceFixture {
    std::string network;
    int64_t validation_time{0};
    CAmount block_subsidy{0};
    std::vector<CBlockHeader> headers;
    CBlock block;
    ConformanceSpendState spend_state;
    ConformanceSpendContext spend_context;
    ConformanceContextualOptions contextual_options;
    Consensus::BlockSpendConsensusOptions spend_options;
    ConformanceExpected expected;
};

struct ConformanceResult {
    bool valid{false};
    Consensus::BlockConsensusStage stage{Consensus::BlockConsensusStage::Structural};
    std::optional<std::string> reject_reason;
    int64_t fees{0};
    int inputs{0};
    int64_t sigop_cost{0};
};

struct ConformanceMismatch {
    std::string field;
    std::string expected;
    std::string actual;
};

ConformanceFixture ReadConformanceFixture(const UniValue& fixture);
[[nodiscard]] Consensus::SnapshotSpendState LoadSnapshotSpendState(const ConformanceFixture& fixture);
[[nodiscard]] Consensus::BlockConsensusContext BuildBlockConsensusContext(const ConformanceFixture& fixture);
[[nodiscard]] Consensus::BlockContextualConsensusOptions BuildBlockContextualConsensusOptions(const ConformanceFixture& fixture);
[[nodiscard]] ConformanceResult RunInternalConsensusFixture(const ConformanceFixture& fixture);
[[nodiscard]] ConformanceResult RunInternalConsensusFixtureStagedApi(const ConformanceFixture& fixture);
[[nodiscard]] ConformanceResult RunCoreSpendStateConsensusFixture(const ConformanceFixture& fixture);
[[nodiscard]] std::optional<ConformanceMismatch> CompareConformanceResult(const ConformanceExpected& expected, const ConformanceResult& actual);

class CoreConformanceAdapter {
public:
    explicit CoreConformanceAdapter(CCoinsViewCache& coins) : m_coins{coins} {}

    void LoadSpendState(const ConformanceSpendState& spend_state);
    [[nodiscard]] validation::CoinsViewSpendState SpendView() const;
    [[nodiscard]] validation::CoinsViewSequenceLockTimeView SequenceLockTimes() const;
    [[nodiscard]] validation::CoinsViewBlockSpendBackend BlockSpendBackend();

private:
    CCoinsViewCache& m_coins;
    std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
};

} // namespace test::consensus

#endif // BITCOIN_TEST_UTIL_CONSENSUS_FIXTURE_H
