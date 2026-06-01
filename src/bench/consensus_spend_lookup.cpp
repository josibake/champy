// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <bench/nanobench.h>
#include <consensus/spend_state_batch.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace {

constexpr std::size_t SPEND_LOOKUP_BENCH_TXS{10'000};

class BenchSpendState final : public Consensus::SpendStateView {
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

struct SpendLookupBenchFixture {
    std::vector<CTransactionRef> transactions;
    std::vector<Consensus::BlockSpentOutputLookup> lookups;
    BenchSpendState spend_state;
};

COutPoint BenchOutPoint(uint8_t txid_value, uint32_t output_index = 0)
{
    return COutPoint{Txid::FromUint256(uint256{txid_value}), output_index};
}

CTransactionRef BenchCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.emplace_back(0, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef BenchSpend(COutPoint prevout)
{
    CMutableTransaction tx;
    tx.vin.emplace_back(prevout);
    tx.vout.emplace_back(1, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

Consensus::CoinSnapshot BenchCoin()
{
    return Consensus::CoinSnapshot{
        .output = CTxOut{2, CScript{} << OP_TRUE},
        .height = 1,
        .is_coinbase = false,
    };
}

SpendLookupBenchFixture MakeSpendLookupBenchFixture()
{
    SpendLookupBenchFixture fixture;
    fixture.transactions.reserve(SPEND_LOOKUP_BENCH_TXS + 1);
    fixture.transactions.push_back(BenchCoinbase());

    for (std::size_t i{0}; i < SPEND_LOOKUP_BENCH_TXS; ++i) {
        const COutPoint external{BenchOutPoint(static_cast<uint8_t>((i % 250) + 1), static_cast<uint32_t>(i))};
        fixture.spend_state.coins.emplace(external, BenchCoin());
        fixture.transactions.push_back(BenchSpend(external));
    }
    fixture.lookups = Consensus::ExtractBlockSpentOutputLookups(fixture.transactions);
    return fixture;
}

void BlockSpendLookupPlan(benchmark::Bench& bench)
{
    const SpendLookupBenchFixture fixture{MakeSpendLookupBenchFixture()};
    bench.unit("block").run([&] {
        const auto plan{Consensus::PlanBlockSpentOutputLookups(fixture.transactions)};
        ankerl::nanobench::doNotOptimizeAway(plan.external_lookups.size());
    });
}

void BlockSpendLookupSort(benchmark::Bench& bench)
{
    const SpendLookupBenchFixture fixture{MakeSpendLookupBenchFixture()};
    bench.unit("block").run([&] {
        const auto sorted{Consensus::SortBlockSpentOutputLookupsByOutpoint(fixture.lookups)};
        ankerl::nanobench::doNotOptimizeAway(sorted.size());
    });
}

void SpendStateBatchAdapter(benchmark::Bench& bench)
{
    const SpendLookupBenchFixture fixture{MakeSpendLookupBenchFixture()};
    const Consensus::SpendStateBatchViewAdapter batch_view{fixture.spend_state};
    const std::vector<COutPoint> outpoints{Consensus::OutpointsForBlockSpentOutputLookups(fixture.lookups)};

    bench.unit("batch").run([&] {
        const auto coins{batch_view.GetCoins(outpoints)};
        ankerl::nanobench::doNotOptimizeAway(coins.size());
    });
}

} // namespace

BENCHMARK(BlockSpendLookupPlan);
BENCHMARK(BlockSpendLookupSort);
BENCHMARK(SpendStateBatchAdapter);
