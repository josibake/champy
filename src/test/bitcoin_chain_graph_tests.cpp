// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/chain_graph/chain_graph.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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

bitcoin::block_hash block_hash_with(std::byte value) noexcept
{
    std::array<std::byte, 32> bytes{};
    bytes.fill(value);
    return bitcoin::block_hash{bytes};
}

bitcoin::block_header header(
    bitcoin::block_hash parent,
    std::uint32_t nonce,
    std::byte merkle_byte = std::byte{0}) noexcept
{
    return bitcoin::block_header{
        1,
        parent,
        merkle(merkle_byte),
        bitcoin::block_time{1231006505 + nonce},
        0x1d00ffff,
        nonce};
}

bool contains(std::span<const bitcoin::block_hash> hashes, bitcoin::block_hash hash) noexcept
{
    return std::ranges::find(hashes, hash) != hashes.end();
}

int check_invariants(const bitcoin::block_index_graph& graph, int line) noexcept
{
    return check(graph.check_invariants().ok(), line);
}

} // namespace

int main()
{
    bitcoin::block_index_graph empty;
    if (auto failure{check(empty.check_invariants().ok(), __LINE__)}) return failure;
    if (auto failure{check(!empty.best_header().has_value(), __LINE__)}) return failure;
    if (auto failure{check(empty.active().empty(), __LINE__)}) return failure;

    bitcoin::block_index_graph graph;
    const auto genesis{header(bitcoin::block_hash{}, 1)};
    const auto genesis_hash{genesis.hash()};
    if (auto failure{check(graph.add_header(genesis, work(1)).changed(), __LINE__)}) return failure;
    if (auto failure{check(graph.add_header(genesis, work(1)).code() == bitcoin::graph_update_code::duplicate_block, __LINE__)}) return failure;
    if (auto failure{check(graph.add_header(header(block_hash_with(std::byte{0x99}), 99), work(2)).code() == bitcoin::graph_update_code::missing_parent, __LINE__)}) return failure;
    if (auto failure{check_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().has_value() && graph.best_header().assume_value().hash() == genesis_hash, __LINE__)}) return failure;
    if (auto failure{check(graph.active().empty(), __LINE__)}) return failure;

    if (auto failure{check(graph.add_block_data(genesis_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check(graph.mark_valid(genesis_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.candidates().size() == 1 && graph.candidates().front() == genesis_hash, __LINE__)}) return failure;
    if (auto failure{check(graph.active().size() == 1 && graph.active().hashes().front() == genesis_hash, __LINE__)}) return failure;

    const auto main_child{header(genesis_hash, 2, std::byte{0x02})};
    const auto main_child_hash{main_child.hash()};
    if (auto failure{check(graph.add_header(main_child, work(2)).changed(), __LINE__)}) return failure;
    if (auto failure{check(graph.best_header().assume_value().hash() == main_child_hash, __LINE__)}) return failure;
    if (auto failure{check(graph.active().hashes().back() == genesis_hash, __LINE__)}) return failure;
    if (auto failure{check(graph.add_block_data(main_child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check(graph.mark_valid(main_child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.active().size() == 2 && graph.active().hashes().back() == main_child_hash, __LINE__)}) return failure;

    const auto stronger_fork{header(genesis_hash, 3, std::byte{0x03})};
    const auto stronger_fork_hash{stronger_fork.hash()};
    if (auto failure{check(graph.add_header(stronger_fork, work(3)).changed(), __LINE__)}) return failure;
    if (auto failure{check(graph.add_block_data(stronger_fork_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check(graph.mark_valid(stronger_fork_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.active().size() == 2 && graph.active().hashes().back() == stronger_fork_hash, __LINE__)}) return failure;

    if (auto failure{check(graph.invalidate(stronger_fork_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(contains(graph.failed_blocks(), stronger_fork_hash), __LINE__)}) return failure;
    if (auto failure{check(!contains(graph.candidates(), stronger_fork_hash), __LINE__)}) return failure;
    if (auto failure{check(graph.active().hashes().back() == main_child_hash, __LINE__)}) return failure;
    if (auto failure{check(graph.reconsider(stronger_fork_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(graph, __LINE__)}) return failure;
    if (auto failure{check(graph.active().hashes().back() == stronger_fork_hash, __LINE__)}) return failure;

    const auto plan_to_main{graph.plan_reorg_to(main_child_hash)};
    if (auto failure{check(plan_to_main.has_plan(), __LINE__)}) return failure;
    if (auto failure{check(plan_to_main.assume_plan().disconnects().size() == 1, __LINE__)}) return failure;
    if (auto failure{check(plan_to_main.assume_plan().disconnects().front() == stronger_fork_hash, __LINE__)}) return failure;
    if (auto failure{check(plan_to_main.assume_plan().connects().size() == 1, __LINE__)}) return failure;
    if (auto failure{check(plan_to_main.assume_plan().connects().front() == main_child_hash, __LINE__)}) return failure;
    if (auto failure{check(graph.plan_reorg_to(block_hash_with(std::byte{0x88})).code() == bitcoin::reorg_plan_code::missing_block, __LINE__)}) return failure;

    bitcoin::block_index_graph unlinked_graph;
    const auto parent{header(bitcoin::block_hash{}, 10)};
    const auto parent_hash{parent.hash()};
    const auto child{header(parent_hash, 11)};
    const auto child_hash{child.hash()};
    if (auto failure{check(unlinked_graph.add_header(parent, work(1)).changed(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.add_header(child, work(2)).changed(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.add_block_data(child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.mark_valid(child_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(unlinked_graph, __LINE__)}) return failure;
    if (auto failure{check(contains(unlinked_graph.unlinked_blocks(), child_hash), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.active().empty(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.add_block_data(parent_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.mark_valid(parent_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(unlinked_graph, __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.unlinked_blocks().empty(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.active().size() == 2 && unlinked_graph.active().hashes().back() == child_hash, __LINE__)}) return failure;

    if (auto failure{check(unlinked_graph.invalidate(parent_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(unlinked_graph, __LINE__)}) return failure;
    if (auto failure{check(contains(unlinked_graph.failed_blocks(), parent_hash), __LINE__)}) return failure;
    if (auto failure{check(contains(unlinked_graph.failed_blocks(), child_hash), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.active().empty(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.reconsider(parent_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(unlinked_graph, __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.failed_blocks().empty(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.active().hashes().back() == child_hash, __LINE__)}) return failure;

    if (auto failure{check(!unlinked_graph.dirty_blocks().empty(), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.mark_clean(parent_hash).changed(), __LINE__)}) return failure;
    if (auto failure{check_invariants(unlinked_graph, __LINE__)}) return failure;
    if (auto failure{check(!contains(unlinked_graph.dirty_blocks(), parent_hash), __LINE__)}) return failure;
    if (auto failure{check(unlinked_graph.mark_clean(bitcoin::block_hash{}).code() == bitcoin::graph_update_code::missing_block, __LINE__)}) return failure;

    if (auto failure{check(unlinked_graph.invalidate(child_hash).changed(), __LINE__)}) return failure;
    const auto invalid_plan{unlinked_graph.plan_reorg_to(child_hash)};
    if (auto failure{check(invalid_plan.code() == bitcoin::reorg_plan_code::ineligible_candidate, __LINE__)}) return failure;

    return 0;
}
