// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <bench/nanobench.h>
#include <coins.h>
#include <coins_view_spend_state.h>
#include <consensus/snapshot_spend_state.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

constexpr int SPEND_BENCH_INPUTS{1'000};
constexpr int SPEND_BENCH_BLOCK_HEIGHT{2};

class NoScriptChecks final : public Consensus::BlockScriptChecker {
public:
    [[nodiscard]] bool WantsChecks() const override { return false; }
    [[nodiscard]] Consensus::BlockSpendResult<void> Check(const Consensus::TransactionScriptCheckPlan&) override { return {}; }
    [[nodiscard]] Consensus::BlockSpendResult<void> Complete() override { return {}; }
};

struct SpendBackendBenchFixture {
    std::vector<CTransactionRef> transactions;
    std::vector<std::pair<COutPoint, Consensus::CoinSnapshot>> coins;
};

Consensus::CoinSnapshot BenchCoin(CAmount value)
{
    return Consensus::CoinSnapshot{
        .output = CTxOut{value, CScript{} << OP_TRUE},
        .height = 1,
        .is_coinbase = false,
    };
}

SpendBackendBenchFixture MakeSpendBackendBenchFixture()
{
    SpendBackendBenchFixture fixture;
    fixture.transactions.reserve(SPEND_BENCH_INPUTS + 1);
    fixture.coins.reserve(SPEND_BENCH_INPUTS);

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.emplace_back(0, CScript{} << OP_TRUE);
    fixture.transactions.push_back(MakeTransactionRef(coinbase));

    const Txid prev_txid{Txid::FromUint256(uint256::ONE)};
    for (int i{0}; i < SPEND_BENCH_INPUTS; ++i) {
        const COutPoint prevout{prev_txid, static_cast<uint32_t>(i)};
        fixture.coins.emplace_back(prevout, BenchCoin(/*value=*/1'000));

        CMutableTransaction spend;
        spend.vin.emplace_back(prevout);
        spend.vout.emplace_back(999, CScript{} << OP_TRUE);
        fixture.transactions.push_back(MakeTransactionRef(spend));
    }

    return fixture;
}

Consensus::BlockSpendContext BenchSpendContext()
{
    return Consensus::BlockSpendContext{
        .block_height = SPEND_BENCH_BLOCK_HEIGHT,
        .previous_median_time_past = 0,
    };
}

void BenchSpendBackend(benchmark::Bench& bench, const char* name, auto&& setup_workspace)
{
    const SpendBackendBenchFixture fixture{MakeSpendBackendBenchFixture()};
    const Consensus::BlockSpendContext context{BenchSpendContext()};
    NoScriptChecks script_checker;
    std::unique_ptr<Consensus::BlockSpendWorkspace> workspace;

    bench.unit("block")
        .name(name)
        .setup([&] { workspace = setup_workspace(fixture, context); })
        .run([&] {
            const auto result{Consensus::ValidateAndStageBlockTransactions(
                fixture.transactions,
                *workspace,
                script_checker,
                context,
                Consensus::BlockSpendConsensusOptions{})};
            assert(result);
            ankerl::nanobench::doNotOptimizeAway(result->inputs);
        });
}

void SnapshotSpendBackend(benchmark::Bench& bench)
{
    std::unique_ptr<Consensus::SnapshotSpendState> backend;
    BenchSpendBackend(bench, "SnapshotSpendBackend", [&](const SpendBackendBenchFixture& fixture, const Consensus::BlockSpendContext& context) {
        backend = std::make_unique<Consensus::SnapshotSpendState>();
        for (const auto& [outpoint, coin] : fixture.coins) {
            backend->AddCoin(outpoint, coin);
        }
        auto workspace{backend->BeginBlockSpend(context)};
        assert(workspace);
        return std::move(*workspace);
    });
}

void CoinsViewSpendBackend(benchmark::Bench& bench)
{
    std::unique_ptr<CCoinsViewCache> coins;
    std::unique_ptr<Consensus::CoinsViewBlockSpendBackend> backend;
    BenchSpendBackend(bench, "CoinsViewSpendBackend", [&](const SpendBackendBenchFixture& fixture, const Consensus::BlockSpendContext& context) {
        coins = std::make_unique<CCoinsViewCache>(&CoinsViewEmpty::Get());
        for (const auto& [outpoint, coin] : fixture.coins) {
            coins->AddCoin(outpoint, Coin{coin.output, coin.height, coin.is_coinbase}, /*possible_overwrite=*/false);
        }
        backend = std::make_unique<Consensus::CoinsViewBlockSpendBackend>(*coins);
        auto workspace{backend->BeginBlockSpend(context)};
        assert(workspace);
        return std::move(*workspace);
    });
}

} // namespace

BENCHMARK(SnapshotSpendBackend);
BENCHMARK(CoinsViewSpendBackend);
