// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/core_adapter/api.h>

#include <arith_uint256.h>
#include <chain.h>
#include <coins.h>
#include <primitives/block.h>
#include <script/script.h>
#include <uint256.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

int check(bool condition, int line) noexcept
{
    return condition ? 0 : line;
}

[[nodiscard]] bool same_bytes(std::span<const std::byte> left, std::span<const std::byte> right) noexcept
{
    return std::ranges::equal(left, right);
}

[[nodiscard]] std::span<const std::byte> bytes_for(const CScript& script) noexcept
{
    return std::as_bytes(std::span<const unsigned char>{script.data(), script.size()});
}

[[nodiscard]] std::span<const std::byte> bytes_for(bitcoin::script_ref script) noexcept
{
    return as_bytes(script);
}

[[nodiscard]] std::span<const std::byte> bytes_for(const bitcoin::witness_item& item) noexcept
{
    return as_bytes(item);
}

[[nodiscard]] CScript true_script()
{
    CScript script;
    script << OP_TRUE;
    return script;
}

[[nodiscard]] CScript data_script()
{
    CScript script;
    const std::vector<unsigned char> bytes{0x11, 0x22, 0x33};
    script << OP_1;
    script << std::span<const unsigned char>{bytes};
    return script;
}

[[nodiscard]] bitcoin::median_time_past mtp(std::int64_t seconds) noexcept
{
    return bitcoin::median_time_past{std::chrono::sys_seconds{std::chrono::seconds{seconds}}};
}

[[nodiscard]] CMutableTransaction core_transaction()
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.nLockTime = 900;

    CTxIn input{COutPoint{Txid::FromUint256(uint256::ONE), 3}, data_script(), 77};
    input.scriptWitness.stack.push_back({0x01, 0x02});
    input.scriptWitness.stack.push_back({0x03});
    tx.vin.push_back(input);
    tx.vout.emplace_back(5'000, true_script());
    return tx;
}

class fixed_coins_view final : public CCoinsView
{
public:
    fixed_coins_view(COutPoint point, Coin coin)
    {
        set(std::move(point), std::move(coin));
    }

    static fixed_coins_view throwing()
    {
        fixed_coins_view view{COutPoint{}, Coin{}};
        view.m_throw = true;
        return view;
    }

    void set(COutPoint point, Coin coin)
    {
        m_entries.push_back(entry{std::move(point), std::move(coin)});
    }

    std::optional<Coin> GetCoin(const COutPoint& outpoint) const override
    {
        if (m_throw) {
            throw std::runtime_error{"lookup failed"};
        }
        const auto match{std::ranges::find_if(m_entries, [&](const entry& candidate) {
            return candidate.point == outpoint;
        })};
        if (match != m_entries.end()) {
            return match->coin;
        }
        return std::nullopt;
    }

    std::optional<Coin> PeekCoin(const COutPoint& outpoint) const override { return GetCoin(outpoint); }
    bool HaveCoin(const COutPoint& outpoint) const override { return GetCoin(outpoint).has_value(); }
    uint256 GetBestBlock() const override { return uint256::ZERO; }
    std::vector<uint256> GetHeadBlocks() const override { return {}; }
    void BatchWrite(CoinsViewCacheCursor&, const uint256&) override {}
    std::unique_ptr<CCoinsViewCursor> Cursor() const override { return nullptr; }
    size_t EstimateSize() const override { return m_entries.size(); }

private:
    struct entry {
        COutPoint point;
        Coin coin;
    };

    std::vector<entry> m_entries;
    bool m_throw{false};
};

class block_index_chain
{
public:
    explicit block_index_chain(std::initializer_list<std::uint32_t> times)
    {
        m_blocks.reserve(times.size());
        for (const auto time : times) {
            CBlockHeader header;
            header.nTime = time;
            auto block{std::make_unique<CBlockIndex>(header)};
            block->nHeight = static_cast<int>(m_blocks.size());
            if (!m_blocks.empty()) {
                block->pprev = m_blocks.back().get();
            }
            m_blocks.push_back(std::move(block));
        }
    }

    [[nodiscard]] const CBlockIndex& tip() const { return *m_blocks.back(); }

private:
    std::vector<std::unique_ptr<CBlockIndex>> m_blocks;
};

int test_header_and_graph()
{
    CBlockHeader core_header;
    core_header.nVersion = 7;
    core_header.hashPrevBlock = uint256::ZERO;
    core_header.hashMerkleRoot = uint256::ONE;
    core_header.nTime = 1231006505;
    core_header.nBits = 0x1d00ffff;
    core_header.nNonce = 42;

    const auto header{bitcoin::core_adapter::to_block_header(core_header)};
    if (auto failure{check(header.version() == core_header.nVersion, __LINE__)}) return failure;
    if (auto failure{check(header.previous_block_hash() == bitcoin::core_adapter::to_block_hash(core_header.hashPrevBlock), __LINE__)}) return failure;
    if (auto failure{check(header.merkle_root() == bitcoin::core_adapter::to_hash256(core_header.hashMerkleRoot), __LINE__)}) return failure;
    if (auto failure{check(header.time().seconds_since_epoch() == core_header.nTime, __LINE__)}) return failure;
    if (auto failure{check(header.bits() == core_header.nBits, __LINE__)}) return failure;
    if (auto failure{check(header.nonce() == core_header.nNonce, __LINE__)}) return failure;

    CBlockIndex index{core_header};
    index.nChainWork = arith_uint256{11};

    bitcoin::block_index_graph graph;
    if (auto failure{check(bitcoin::core_adapter::add_header(graph, index).changed(), __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().has_value(), __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().assume_value().height() == bitcoin::block_height{0}, __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().assume_value().work() == bitcoin::core_adapter::to_chain_work(index.nChainWork), __LINE__)}) return failure;
    if (auto failure{check(graph.check_invariants().ok(), __LINE__)}) return failure;

    return 0;
}

int test_transaction_and_block_conversion()
{
    const CTransaction core_tx{core_transaction()};
    const auto tx{bitcoin::core_adapter::to_transaction(core_tx)};
    if (auto failure{check(tx.version() == 2, __LINE__)}) return failure;
    if (auto failure{check(tx.locktime() == 900, __LINE__)}) return failure;
    if (auto failure{check(tx.inputs().size() == 1, __LINE__)}) return failure;
    if (auto failure{check(tx.outputs().size() == 1, __LINE__)}) return failure;

    const auto& input{tx.inputs().front()};
    if (auto failure{check(input.previous_output().index() == bitcoin::tx_output_index{3}, __LINE__)}) return failure;
    if (auto failure{check(input.sequence() == 77, __LINE__)}) return failure;
    if (auto failure{check(same_bytes(bytes_for(input.script()), bytes_for(core_tx.vin.front().scriptSig)), __LINE__)}) return failure;
    if (auto failure{check(input.witness().size() == 2, __LINE__)}) return failure;
    if (auto failure{check(same_bytes(bytes_for(input.witness()[0]), std::as_bytes(std::span<const unsigned char>{core_tx.vin.front().scriptWitness.stack[0]})), __LINE__)}) return failure;
    if (auto failure{check(same_bytes(bytes_for(input.witness()[1]), std::as_bytes(std::span<const unsigned char>{core_tx.vin.front().scriptWitness.stack[1]})), __LINE__)}) return failure;

    const auto& output{tx.outputs().front()};
    if (auto failure{check(output.value() == bitcoin::amount{5'000}, __LINE__)}) return failure;
    if (auto failure{check(same_bytes(bytes_for(output.script()), bytes_for(core_tx.vout.front().scriptPubKey)), __LINE__)}) return failure;

    CBlockHeader core_header;
    core_header.nVersion = 4;
    core_header.hashMerkleRoot = uint256{2};
    core_header.nTime = 777;
    core_header.nBits = 0x1d00ffff;
    core_header.nNonce = 5;

    CBlock core_block{core_header};
    core_block.vtx.push_back(MakeTransactionRef(core_tx));
    const auto block{bitcoin::core_adapter::to_block(core_block)};
    if (auto failure{check(block.header() == bitcoin::core_adapter::to_block_header(core_header), __LINE__)}) return failure;
    if (auto failure{check(block.transactions().size() == 1, __LINE__)}) return failure;
    if (auto failure{check(block.transactions().front() == tx, __LINE__)}) return failure;

    return 0;
}

int test_coin_snapshot()
{
    const auto previous_mtp{mtp(1234)};
    const COutPoint core_point{Txid::FromUint256(uint256::ONE), 9};
    const Coin core_coin{CTxOut{42, true_script()}, 144, true};
    fixed_coins_view view{core_point, core_coin};

    const auto converted_coin{bitcoin::core_adapter::to_coin(core_coin, previous_mtp)};
    if (auto failure{check(converted_coin.output().value() == bitcoin::amount{42}, __LINE__)}) return failure;
    if (auto failure{check(converted_coin.height() == bitcoin::block_height{144}, __LINE__)}) return failure;
    if (auto failure{check(converted_coin.coinbase(), __LINE__)}) return failure;
    if (auto failure{check(converted_coin.previous_median_time_past() == previous_mtp, __LINE__)}) return failure;

    const bitcoin::core_adapter::core_coin_snapshot snapshot{view, previous_mtp};
    static_assert(bitcoin::coin_index<bitcoin::core_adapter::core_coin_snapshot>);
    static_assert(!bitcoin::fallible_coin_source<bitcoin::core_adapter::core_coin_snapshot>);
    const auto found{snapshot(bitcoin::core_adapter::to_outpoint(core_point))};
    if (auto failure{check(found.has_value(), __LINE__)}) return failure;
    if (auto failure{check(*found == converted_coin, __LINE__)}) return failure;

    const auto missing{snapshot(bitcoin::outpoint{bitcoin::txid{}, bitcoin::tx_output_index{1}})};
    if (auto failure{check(!missing.has_value(), __LINE__)}) return failure;

    fixed_coins_view spent_view{core_point, Coin{}};
    const bitcoin::core_adapter::core_coin_snapshot spent_snapshot{spent_view, previous_mtp};
    const auto spent{spent_snapshot(bitcoin::core_adapter::to_outpoint(core_point))};
    if (auto failure{check(!spent.has_value(), __LINE__)}) return failure;

    auto throwing_view{fixed_coins_view::throwing()};
    const bitcoin::core_adapter::core_coin_snapshot throwing_snapshot{throwing_view, previous_mtp};
    bool threw{false};
    try {
        (void)throwing_snapshot(bitcoin::core_adapter::to_outpoint(core_point));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    if (auto failure{check(threw, __LINE__)}) return failure;

    const bitcoin::core_adapter::core_coin_source source{view, previous_mtp};
    static_assert(!bitcoin::coin_index<bitcoin::core_adapter::core_coin_source>);
    static_assert(bitcoin::fallible_coin_source<bitcoin::core_adapter::core_coin_source>);
    const auto rich_found{source.lookup(bitcoin::core_adapter::to_outpoint(core_point))};
    if (auto failure{check(rich_found.is_found(), __LINE__)}) return failure;
    if (auto failure{check(rich_found.assume_value() == converted_coin, __LINE__)}) return failure;

    const auto rich_missing{source.lookup(bitcoin::outpoint{bitcoin::txid{}, bitcoin::tx_output_index{1}})};
    if (auto failure{check(rich_missing.is_missing(), __LINE__)}) return failure;

    const bitcoin::core_adapter::core_coin_source spent_source{spent_view, previous_mtp};
    const auto rich_spent{spent_source.lookup(bitcoin::core_adapter::to_outpoint(core_point))};
    if (auto failure{check(rich_spent.is_spent(), __LINE__)}) return failure;

    const bitcoin::core_adapter::core_coin_source throwing_source{throwing_view, previous_mtp};
    const auto failed{throwing_source.lookup(bitcoin::core_adapter::to_outpoint(core_point))};
    if (auto failure{check(failed.state() == bitcoin::coin_lookup_state::io_failure, __LINE__)}) return failure;

    block_index_chain chain{1000, 2000, 3000, 4000, 5000};
    const COutPoint older_point{Txid::FromUint256(uint256{2}), 0};
    const COutPoint newer_point{Txid::FromUint256(uint256{3}), 0};
    fixed_coins_view chain_view{older_point, Coin{CTxOut{1, true_script()}, 1, false}};
    chain_view.set(newer_point, Coin{CTxOut{2, true_script()}, 3, false});

    const bitcoin::core_adapter::core_coin_snapshot chain_snapshot{chain_view, chain.tip()};
    const auto older_coin{chain_snapshot(bitcoin::core_adapter::to_outpoint(older_point))};
    if (auto failure{check(older_coin.has_value(), __LINE__)}) return failure;
    if (auto failure{check(older_coin->previous_median_time_past() == mtp(1000), __LINE__)}) return failure;

    const auto newer_coin{chain_snapshot(bitcoin::core_adapter::to_outpoint(newer_point))};
    if (auto failure{check(newer_coin.has_value(), __LINE__)}) return failure;
    if (auto failure{check(newer_coin->previous_median_time_past() == mtp(2000), __LINE__)}) return failure;

    const COutPoint unwitnessed_point{Txid::FromUint256(uint256{4}), 0};
    fixed_coins_view unwitnessed_view{unwitnessed_point, Coin{CTxOut{3, true_script()}, 10, false}};
    const bitcoin::core_adapter::core_coin_snapshot unwitnessed_snapshot{unwitnessed_view, chain.tip()};
    threw = false;
    try {
        (void)unwitnessed_snapshot(bitcoin::core_adapter::to_outpoint(unwitnessed_point));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    if (auto failure{check(threw, __LINE__)}) return failure;

    const bitcoin::core_adapter::core_coin_source unwitnessed_source{unwitnessed_view, chain.tip()};
    const auto unavailable{unwitnessed_source.lookup(bitcoin::core_adapter::to_outpoint(unwitnessed_point))};
    if (auto failure{check(unavailable.state() == bitcoin::coin_lookup_state::unavailable, __LINE__)}) return failure;

    return 0;
}

int test_chain_graph_snapshot()
{
    CBlockHeader genesis_header;
    genesis_header.nVersion = 1;
    genesis_header.hashMerkleRoot = uint256::ONE;
    genesis_header.nTime = 1;
    genesis_header.nBits = 0x1d00ffff;
    genesis_header.nNonce = 1;

    CBlockIndex genesis{genesis_header};
    const auto genesis_hash{genesis_header.GetHash()};
    genesis.phashBlock = &genesis_hash;
    genesis.nChainWork = arith_uint256{1};

    CBlockHeader child_header;
    child_header.nVersion = 1;
    child_header.hashPrevBlock = genesis_hash;
    child_header.hashMerkleRoot = uint256{2};
    child_header.nTime = 2;
    child_header.nBits = 0x1d00ffff;
    child_header.nNonce = 2;

    CBlockIndex child{child_header};
    const auto child_hash{child_header.GetHash()};
    child.phashBlock = &child_hash;
    child.pprev = &genesis;
    child.nHeight = 1;
    child.nChainWork = arith_uint256{3};
    genesis.nStatus = BLOCK_HAVE_DATA | BLOCK_VALID_SCRIPTS;
    child.nStatus = BLOCK_HAVE_DATA | BLOCK_VALID_SCRIPTS;

    const auto graph{bitcoin::core_adapter::to_chain_graph_snapshot(child)};
    if (auto failure{check(graph.check_invariants().ok(), __LINE__)}) return failure;
    if (auto failure{check(graph.blocks().size() == 2, __LINE__)}) return failure;
    if (auto failure{check(graph.active().size() == 2, __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().has_value(), __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().assume_value().height() == bitcoin::block_height{1}, __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().assume_value().work() == bitcoin::core_adapter::to_chain_work(child.nChainWork), __LINE__)}) return failure;

    return 0;
}

} // namespace

int main()
{
    if (auto failure{test_header_and_graph()}) return failure;
    if (auto failure{test_transaction_and_block_conversion()}) return failure;
    if (auto failure{test_coin_snapshot()}) return failure;
    if (auto failure{test_chain_graph_snapshot()}) return failure;
    return 0;
}
