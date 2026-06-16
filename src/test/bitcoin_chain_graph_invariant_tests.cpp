// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/chain_graph/chain_graph.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

int check(bool condition, int line) noexcept
{
    return condition ? 0 : line;
}

bitcoin::chain_work work(std::uint64_t value) noexcept
{
    std::array<std::byte, 32> bytes{};
    for (std::size_t index{0}; index < sizeof(value); ++index) {
        bytes[index] = static_cast<std::byte>((value >> (8U * index)) & 0xffU);
    }
    return bitcoin::chain_work{bytes};
}

bitcoin::hash256 merkle(std::byte value) noexcept
{
    std::array<std::byte, 32> bytes{};
    bytes.fill(value);
    return bitcoin::hash256{bytes};
}

bitcoin::block_header header(bitcoin::block_hash parent, std::uint32_t nonce) noexcept
{
    return bitcoin::block_header{
        1,
        parent,
        merkle(static_cast<std::byte>(nonce)),
        bitcoin::block_time{1231006505 + nonce},
        0x1d00ffff,
        nonce};
}

int require_invariants(const bitcoin::block_index_graph& graph, int line) noexcept
{
    return check(graph.check_invariants().ok(), line);
}

} // namespace

int main()
{
    bitcoin::block_index_graph graph;
    const auto root{header(bitcoin::block_hash{}, 1)};
    const auto root_hash{root.hash()};
    const auto child{header(root_hash, 2)};
    const auto child_hash{child.hash()};
    const auto grandchild{header(child_hash, 3)};
    const auto grandchild_hash{grandchild.hash()};

    if (auto failure{check(graph.add_header(root, work(1)).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.add_header(child, work(2)).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.add_header(grandchild, work(3)).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;

    if (auto failure{check(graph.add_block_data(grandchild_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.mark_valid(grandchild_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(!graph.unlinked_blocks().empty(), __LINE__)}) return failure;

    if (auto failure{check(graph.add_block_data(root_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.mark_valid(root_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.add_block_data(child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.mark_valid(child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;

    if (auto failure{check(graph.invalidate(child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.reconsider(child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.mark_clean(root_hash).changed(), __LINE__)}) return failure;
    if (auto failure{require_invariants(graph, __LINE__)}) return failure;

    return 0;
}
