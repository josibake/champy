// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/chain_graph/chain_graph.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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

bitcoin::hash256 merkle(std::uint32_t value) noexcept
{
    std::array<std::byte, 32> bytes{};
    bytes[0] = static_cast<std::byte>(value);
    bytes[1] = static_cast<std::byte>(value >> 8U);
    return bitcoin::hash256{bytes};
}

bitcoin::block_header header(bitcoin::block_hash parent, std::uint32_t nonce) noexcept
{
    return bitcoin::block_header{
        1,
        parent,
        merkle(nonce),
        bitcoin::block_time{1231006505 + nonce},
        0x1d00ffff,
        nonce};
}

bool eligible(std::size_t index, std::span<const int> parents, std::uint32_t data_mask, std::uint32_t valid_mask) noexcept
{
    for (std::size_t cursor{index};;) {
        const auto bit{1U << cursor};
        if ((data_mask & bit) == 0 || (valid_mask & bit) == 0) {
            return false;
        }
        if (parents[cursor] < 0) {
            return true;
        }
        cursor = static_cast<std::size_t>(parents[cursor]);
    }
}

bool unlinked(std::size_t index, std::span<const int> parents, std::uint32_t data_mask, std::uint32_t valid_mask) noexcept
{
    const auto bit{1U << index};
    return (data_mask & bit) != 0 &&
           (valid_mask & bit) != 0 &&
           !eligible(index, parents, data_mask, valid_mask);
}

std::size_t height_of(std::size_t index, std::span<const int> parents) noexcept
{
    std::size_t height{0};
    for (std::size_t cursor{index}; parents[cursor] >= 0; cursor = static_cast<std::size_t>(parents[cursor])) {
        ++height;
    }
    return height;
}

int expected_tip(
    std::span<const bitcoin::block_hash> hashes,
    std::span<const int> parents,
    std::uint32_t data_mask,
    std::uint32_t valid_mask) noexcept
{
    int best{-1};
    for (std::size_t index{0}; index < hashes.size(); ++index) {
        if (!eligible(index, parents, data_mask, valid_mask)) {
            continue;
        }
        if (best < 0) {
            best = static_cast<int>(index);
            continue;
        }
        if (index > static_cast<std::size_t>(best)) {
            best = static_cast<int>(index);
        } else if (index == static_cast<std::size_t>(best) &&
                   height_of(index, parents) > height_of(static_cast<std::size_t>(best), parents)) {
            best = static_cast<int>(index);
        } else if (index == static_cast<std::size_t>(best) && hashes[index] < hashes[static_cast<std::size_t>(best)]) {
            best = static_cast<int>(index);
        }
    }
    return best;
}

int run_case(std::span<const int> parents, std::uint32_t data_mask, std::uint32_t valid_mask)
{
    bitcoin::block_index_graph graph;
    std::vector<bitcoin::block_hash> hashes(parents.size());
    for (std::size_t index{0}; index < parents.size(); ++index) {
        const auto parent_hash{
            parents[index] < 0 ?
                bitcoin::block_hash{} :
                hashes[static_cast<std::size_t>(parents[index])]};
        const auto candidate{header(parent_hash, static_cast<std::uint32_t>(index + 1))};
        hashes[index] = candidate.hash();
        if (!graph.add_header(candidate, work(index + 1)).changed()) {
            return 10;
        }
        if ((data_mask & (1U << index)) != 0 && !graph.add_block_data(hashes[index]).changed()) {
            return 11;
        }
        if ((valid_mask & (1U << index)) != 0 && !graph.mark_valid(hashes[index]).changed()) {
            return 12;
        }
        if (!graph.check_invariants().ok()) {
            return 13;
        }
    }

    const auto tip{expected_tip(hashes, parents, data_mask, valid_mask)};
    if (tip < 0) {
        if (!graph.active().empty()) {
            return 14;
        }
    } else {
        const auto active{graph.active()};
        if (active.empty() || active.hashes().back() != hashes[static_cast<std::size_t>(tip)]) {
            return 15;
        }
    }

    for (std::size_t index{0}; index < parents.size(); ++index) {
        const auto is_unlinked{unlinked(index, parents, data_mask, valid_mask)};
        const auto listed{
            std::ranges::find(graph.unlinked_blocks(), hashes[index]) != graph.unlinked_blocks().end()};
        if (is_unlinked != listed) {
            return 16;
        }
    }

    return 0;
}

int enumerate_parents(std::vector<int>& parents, std::size_t index)
{
    if (index == parents.size()) {
        const auto variants{1U << parents.size()};
        for (std::uint32_t data_mask{0}; data_mask < variants; ++data_mask) {
            for (std::uint32_t valid_mask{0}; valid_mask < variants; ++valid_mask) {
                if (auto failure{run_case(parents, data_mask, valid_mask)}) {
                    return failure;
                }
            }
        }
        return 0;
    }

    for (std::size_t parent{0}; parent < index; ++parent) {
        parents[index] = static_cast<int>(parent);
        if (auto failure{enumerate_parents(parents, index + 1)}) {
            return failure;
        }
    }
    return 0;
}

} // namespace

int main()
{
    for (std::size_t size{1}; size <= 4; ++size) {
        std::vector<int> parents(size, -1);
        if (auto failure{enumerate_parents(parents, 1)}) {
            return failure;
        }
    }

    return check(run_case(std::span<const int>{}, 0, 0) == 0, __LINE__);
}
