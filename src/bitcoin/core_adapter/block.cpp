// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/core_adapter/block.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace bitcoin::core_adapter {
namespace {

[[nodiscard]] std::array<std::byte, 32> bytes_from(const uint256& value) noexcept
{
    std::array<std::byte, 32> bytes{};
    std::ranges::transform(
        std::span<const unsigned char>{value.data(), uint256::size()},
        bytes.begin(),
        [](unsigned char byte) { return static_cast<std::byte>(byte); });
    return bytes;
}

template <typename Hash>
[[nodiscard]] uint256 uint256_from(Hash value) noexcept
{
    std::array<unsigned char, 32> bytes{};
    std::ranges::transform(as_bytes(value), bytes.begin(), [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
    return uint256{std::span<const unsigned char>{bytes}};
}

[[nodiscard]] block_hash hash_for(const CBlockIndex& index) noexcept
{
    return to_block_header(index.GetBlockHeader()).hash();
}

void copy_status(block_index_graph& graph, const CBlockIndex& index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    const auto hash{hash_for(index)};
    if ((index.nStatus & BLOCK_HAVE_DATA) != 0) {
        const auto data_result{graph.add_block_data(hash)};
        if (!data_result.changed()) {
            return;
        }
    }
    if ((index.nStatus & BLOCK_FAILED_VALID) != 0) {
        const auto failure_result{graph.invalidate(hash)};
        (void)failure_result;
        return;
    }
    if ((index.nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) {
        const auto valid_result{graph.mark_valid(hash)};
        (void)valid_result;
    }
}

} // namespace

hash256 to_hash256(const uint256& value) noexcept
{
    return hash256{bytes_from(value)};
}

block_hash to_block_hash(const uint256& value) noexcept
{
    return block_hash{bytes_from(value)};
}

chain_work to_chain_work(const arith_uint256& value) noexcept
{
    return chain_work{bytes_from(ArithToUint256(value))};
}

block_header to_block_header(const CBlockHeader& header) noexcept
{
    return block_header{
        header.nVersion,
        to_block_hash(header.hashPrevBlock),
        to_hash256(header.hashMerkleRoot),
        block_time{header.nTime},
        header.nBits,
        header.nNonce};
}

block to_block(const CBlock& block)
{
    std::vector<transaction> transactions;
    transactions.reserve(block.vtx.size());
    std::ranges::transform(block.vtx, std::back_inserter(transactions), [](const CTransactionRef& tx) {
        return to_transaction(*tx);
    });
    return bitcoin::block{to_block_header(block), std::move(transactions)};
}

uint256 to_uint256(hash256 value) noexcept
{
    return uint256_from(value);
}

uint256 to_uint256(block_hash value) noexcept
{
    return uint256_from(value);
}

CBlockHeader to_core_block_header(const block_header& header) noexcept
{
    CBlockHeader result;
    result.nVersion = header.version();
    result.hashPrevBlock = to_uint256(header.previous_block_hash());
    result.hashMerkleRoot = to_uint256(header.merkle_root());
    result.nTime = header.time().seconds_since_epoch();
    result.nBits = header.bits();
    result.nNonce = header.nonce();
    return result;
}

CBlock to_core_block(const block& value)
{
    CBlock result{to_core_block_header(value.header())};
    result.vtx.reserve(value.transactions().size());
    std::ranges::transform(value.transactions(), std::back_inserter(result.vtx), [](const transaction& tx) {
        return MakeTransactionRef(to_core_transaction(tx));
    });
    return result;
}

graph_update_result add_header(block_index_graph& graph, const CBlockIndex& index)
{
    return graph.add_header(to_block_header(index.GetBlockHeader()), to_chain_work(index.nChainWork));
}

block_index_graph to_chain_graph_snapshot(const CBlockIndex& tip)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    std::vector<const CBlockIndex*> branch;
    for (const CBlockIndex* cursor{&tip}; cursor != nullptr; cursor = cursor->pprev) {
        branch.push_back(cursor);
    }
    std::ranges::reverse(branch);

    block_index_graph graph;
    for (const auto* index : branch) {
        const auto added{add_header(graph, *index)};
        if (!added.changed()) {
            continue;
        }
        copy_status(graph, *index);
    }
    return graph;
}

} // namespace bitcoin::core_adapter
