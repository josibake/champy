// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <bench/bench.h>
#include <bench/nanobench.h>
#include <consensus/segment_spend.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <cassert>
#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace {

constexpr std::size_t SEGMENT_SPEND_BENCH_BLOCKS{32};
constexpr std::size_t SEGMENT_SPEND_BENCH_TXS_PER_BLOCK{64};

uint256 Uint(uint64_t value)
{
    return ArithToUint256(arith_uint256{value});
}

CTransactionRef BenchCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef BenchSpend(const COutPoint& prevout)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.emplace_back(prevout);
    tx.vout.emplace_back(999, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

Consensus::CoinSnapshot BenchCoin(int height)
{
    return {
        .output = CTxOut{1'000, CScript{} << OP_TRUE},
        .height = height,
        .is_coinbase = false,
    };
}

Consensus::SegmentBlockContext BenchBlockContext(std::size_t block_index)
{
    const int height{static_cast<int>(100 + block_index)};
    return {
        .hash = Uint(height),
        .parent_hash = Uint(height - 1),
        .height = height,
        .previous_median_time_past = static_cast<int64_t>(height) * 600,
        .block_subsidy = 50,
    };
}

class BenchSegmentSpendView final : public Consensus::SegmentSpendBatchView {
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

struct SegmentSpendBenchFixture {
    std::vector<std::vector<CTransactionRef>> transactions_by_block;
    std::vector<Consensus::SegmentBlockView> blocks;
    BenchSegmentSpendView spend_view;
};

SegmentSpendBenchFixture MakeSegmentSpendBenchFixture()
{
    SegmentSpendBenchFixture fixture;
    fixture.transactions_by_block.reserve(SEGMENT_SPEND_BENCH_BLOCKS);

    for (std::size_t block_index{0}; block_index < SEGMENT_SPEND_BENCH_BLOCKS; ++block_index) {
        std::vector<CTransactionRef>& transactions{fixture.transactions_by_block.emplace_back()};
        transactions.reserve(SEGMENT_SPEND_BENCH_TXS_PER_BLOCK + 1);
        transactions.push_back(BenchCoinbase());

        for (std::size_t tx_index{0}; tx_index < SEGMENT_SPEND_BENCH_TXS_PER_BLOCK; ++tx_index) {
            const uint64_t id{1 + block_index * SEGMENT_SPEND_BENCH_TXS_PER_BLOCK + tx_index};
            const COutPoint prevout{Txid::FromUint256(Uint(id)), 0};
            fixture.spend_view.coins.emplace(prevout, Consensus::SegmentCoinSnapshot{
                .coin = BenchCoin(/*height=*/1),
                .previous_median_time_past = 600,
            });
            transactions.push_back(BenchSpend(prevout));
        }
    }

    fixture.blocks.reserve(fixture.transactions_by_block.size());
    for (std::size_t block_index{0}; block_index < fixture.transactions_by_block.size(); ++block_index) {
        fixture.blocks.push_back({
            .context = BenchBlockContext(block_index),
            .transactions = fixture.transactions_by_block[block_index],
        });
    }
    return fixture;
}

void SegmentSpendInputPlan(benchmark::Bench& bench)
{
    const SegmentSpendBenchFixture fixture{MakeSegmentSpendBenchFixture()};
    bench.unit("segment").run([&] {
        const auto plan{Consensus::PlanSegmentSpendInputs(fixture.blocks)};
        ankerl::nanobench::doNotOptimizeAway(plan.external_lookups.size());
    });
}

void SegmentSpendValidate(benchmark::Bench& bench)
{
    const SegmentSpendBenchFixture fixture{MakeSegmentSpendBenchFixture()};
    bench.unit("segment").run([&] {
        const auto validation{Consensus::ValidateSegmentSpend(
            fixture.blocks,
            fixture.spend_view,
            Consensus::ScriptCheckPlanCollection::Skip)};
        assert(validation);
        ankerl::nanobench::doNotOptimizeAway(validation->summary.spent_outputs.size());
    });
}

void SegmentSpendValidateWithScriptPlans(benchmark::Bench& bench)
{
    const SegmentSpendBenchFixture fixture{MakeSegmentSpendBenchFixture()};
    bench.unit("segment").run([&] {
        const auto validation{Consensus::ValidateSegmentSpend(
            fixture.blocks,
            fixture.spend_view,
            Consensus::ScriptCheckPlanCollection::Collect)};
        assert(validation);
        ankerl::nanobench::doNotOptimizeAway(validation->block_stages.size());
    });
}

void SegmentAccumulatorFinalize(benchmark::Bench& bench)
{
    const SegmentSpendBenchFixture fixture{MakeSegmentSpendBenchFixture()};
    const auto validation{Consensus::ValidateSegmentSpend(
        fixture.blocks,
        fixture.spend_view,
        Consensus::ScriptCheckPlanCollection::Skip)};
    assert(validation);

    bench.unit("segment").run([&] {
        Consensus::AdditiveSegmentAccumulator accumulator;
        const auto artifact{Consensus::FinalizeSegmentSpend(fixture.blocks, validation->summary, accumulator)};
        ankerl::nanobench::doNotOptimizeAway(artifact.accumulator_root);
    });
}

} // namespace

BENCHMARK(SegmentSpendInputPlan);
BENCHMARK(SegmentSpendValidate);
BENCHMARK(SegmentSpendValidateWithScriptPlans);
BENCHMARK(SegmentAccumulatorFinalize);
