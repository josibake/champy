// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/chain_graph/chain_graph.h>

#include <algorithm>
#include <ranges>
#include <utility>

namespace bitcoin {
namespace {

[[nodiscard]] bool is_genesis_parent(block_hash hash) noexcept
{
    return !static_cast<bool>(hash);
}

[[nodiscard]] bool same_hashes(std::span<const block_hash> left, std::span<const block_hash> right) noexcept
{
    return std::ranges::equal(left, right);
}

void sort_hashes(std::vector<block_hash>& hashes)
{
    std::ranges::sort(hashes);
}

} // namespace

graph_update_result block_index_graph::add_header(const block_header& header, chain_work work)
{
    const auto hash{header.hash()};
    if (find_node(hash) != nullptr) {
        return graph_update_result::duplicate_block();
    }

    const auto parent_hash{header.previous_block_hash()};
    block_height height{0};
    if (!is_genesis_parent(parent_hash)) {
        const auto* parent{find_node(parent_hash)};
        if (parent == nullptr) {
            return graph_update_result::missing_parent();
        }
        height = block_height{parent->height.value() + 1};
    }

    const auto node_index{m_nodes.size()};
    m_nodes.push_back(node{
        hash,
        parent_hash,
        header,
        height,
        work,
        false,
        false,
        false,
        true});
    try {
        m_node_by_hash.emplace(hash, node_index);
    } catch (...) {
        m_nodes.pop_back();
        throw;
    }
    rebuild_indexes();
    return graph_update_result::applied();
}

graph_update_result block_index_graph::add_block_data(block_hash hash)
{
    auto* node{find_node(hash)};
    if (node == nullptr) {
        return graph_update_result::missing_block();
    }
    node->has_block_data = true;
    node->dirty = true;
    rebuild_indexes();
    return graph_update_result::applied();
}

graph_update_result block_index_graph::mark_valid(block_hash hash)
{
    auto* node{find_node(hash)};
    if (node == nullptr) {
        return graph_update_result::missing_block();
    }
    node->valid = true;
    node->dirty = true;
    rebuild_indexes();
    return graph_update_result::applied();
}

graph_update_result block_index_graph::invalidate(block_hash hash)
{
    auto* node{find_node(hash)};
    if (node == nullptr) {
        return graph_update_result::missing_block();
    }
    node->failed = true;
    node->dirty = true;
    for (auto& descendant : m_nodes) {
        const auto* cursor{&descendant};
        while (!is_genesis_parent(cursor->parent_hash)) {
            const auto* parent{find_node(cursor->parent_hash)};
            if (parent == nullptr) {
                break;
            }
            if (parent->hash == hash) {
                descendant.dirty = true;
                break;
            }
            cursor = parent;
        }
    }
    rebuild_indexes();
    return graph_update_result::applied();
}

graph_update_result block_index_graph::reconsider(block_hash hash)
{
    auto* node{find_node(hash)};
    if (node == nullptr) {
        return graph_update_result::missing_block();
    }

    for (auto& candidate : m_nodes) {
        if (candidate.hash == hash) {
            candidate.failed = false;
            candidate.dirty = true;
            continue;
        }
        const auto* cursor{&candidate};
        while (!is_genesis_parent(cursor->parent_hash)) {
            const auto* parent{find_node(cursor->parent_hash)};
            if (parent == nullptr) {
                break;
            }
            if (parent->hash == hash) {
                candidate.failed = false;
                candidate.dirty = true;
                break;
            }
            cursor = parent;
        }
    }

    rebuild_indexes();
    return graph_update_result::applied();
}

graph_update_result block_index_graph::mark_clean(block_hash hash)
{
    auto* node{find_node(hash)};
    if (node == nullptr) {
        return graph_update_result::missing_block();
    }
    node->dirty = false;
    rebuild_indexes();
    return graph_update_result::applied();
}

chain_graph_block_lookup block_index_graph::find(block_hash hash) const
{
    const auto* node{find_node(hash)};
    if (node == nullptr) {
        return chain_graph_block_lookup::missing();
    }
    return chain_graph_block_lookup::found(block_for(*node));
}

chain_graph_block_lookup block_index_graph::best_header() const
{
    if (!m_has_best_header) {
        return chain_graph_block_lookup::missing();
    }
    return find(m_best_header);
}

reorg_plan_result block_index_graph::plan_reorg_to(block_hash target) const
{
    const auto* target_node{find_node(target)};
    if (target_node == nullptr) {
        return reorg_plan_result::missing_block();
    }
    if (!eligible_candidate(*target_node)) {
        return reorg_plan_result::ineligible_candidate();
    }

    const auto target_path{path_to_root(*target_node)};
    if (m_candidates.empty()) {
        std::vector<block_hash> connects;
        connects.reserve(target_path.size());
        for (auto cursor{target_path.rbegin()}; cursor != target_path.rend(); ++cursor) {
            connects.push_back((*cursor)->hash);
        }
        return reorg_plan_result::planned(reorg_plan{{}, std::move(connects)});
    }

    const auto* active_tip{find_node(m_candidates.front())};
    if (active_tip == nullptr) {
        return reorg_plan_result::ineligible_candidate();
    }

    const auto active_path{path_to_root(*active_tip)};
    const node* fork{nullptr};
    for (const auto* active_node : active_path) {
        const auto match{std::ranges::find(target_path, active_node->hash, &node::hash)};
        if (match != target_path.end()) {
            fork = active_node;
            break;
        }
    }
    if (fork == nullptr) {
        return reorg_plan_result::ineligible_candidate();
    }

    std::vector<block_hash> disconnects;
    for (const auto* active_node : active_path) {
        if (active_node->hash == fork->hash) {
            break;
        }
        disconnects.push_back(active_node->hash);
    }

    std::vector<block_hash> connects;
    for (const auto* target_ancestor : target_path) {
        if (target_ancestor->hash == fork->hash) {
            break;
        }
        connects.push_back(target_ancestor->hash);
    }
    std::ranges::reverse(connects);
    return reorg_plan_result::planned(reorg_plan{std::move(disconnects), std::move(connects)});
}

active_chain block_index_graph::active() const
{
    if (m_candidates.empty()) {
        return active_chain{};
    }

    std::vector<block_hash> hashes;
    std::vector<block_header> headers;
    const node* cursor{find_node(m_candidates.front())};
    while (cursor != nullptr) {
        hashes.push_back(cursor->hash);
        headers.push_back(cursor->header);
        if (is_genesis_parent(cursor->parent_hash)) {
            break;
        }
        cursor = find_node(cursor->parent_hash);
    }
    std::ranges::reverse(hashes);
    std::ranges::reverse(headers);
    return active_chain{std::move(hashes), std::move(headers)};
}

chain_graph_check block_index_graph::check_invariants() const
{
    if (m_node_by_hash.size() != m_nodes.size()) {
        return chain_graph_check::invalid(chain_graph_invariant::stale_hash_index);
    }

    std::vector<block_hash> seen;
    seen.reserve(m_nodes.size());
    for (std::size_t node_index{0}; node_index < m_nodes.size(); ++node_index) {
        const auto& node{m_nodes[node_index]};
        if (std::ranges::find(seen, node.hash) != seen.end()) {
            return chain_graph_check::invalid(chain_graph_invariant::duplicate_hash);
        }
        seen.push_back(node.hash);

        const auto indexed{m_node_by_hash.find(node.hash)};
        if (indexed == m_node_by_hash.end() || indexed->second != node_index) {
            return chain_graph_check::invalid(chain_graph_invariant::stale_hash_index);
        }

        if (is_genesis_parent(node.parent_hash)) {
            if (node.height != block_height{0}) {
                return chain_graph_check::invalid(chain_graph_invariant::height_mismatch);
            }
            continue;
        }

        const auto* parent{find_node(node.parent_hash)};
        if (parent == nullptr) {
            return chain_graph_check::invalid(chain_graph_invariant::missing_parent);
        }
        if (node.height.value() != parent->height.value() + 1) {
            return chain_graph_check::invalid(chain_graph_invariant::height_mismatch);
        }
    }

    const auto candidates{recompute_candidates()};
    if (!same_hashes(candidates, m_candidates)) {
        return chain_graph_check::invalid(chain_graph_invariant::stale_candidate_index);
    }

    const auto unlinked{recompute_unlinked()};
    if (!same_hashes(unlinked, m_unlinked)) {
        return chain_graph_check::invalid(chain_graph_invariant::stale_unlinked_index);
    }

    const auto failed{recompute_failed()};
    if (!same_hashes(failed, m_failed)) {
        return chain_graph_check::invalid(chain_graph_invariant::stale_failed_index);
    }

    const auto dirty{recompute_dirty()};
    if (!same_hashes(dirty, m_dirty)) {
        return chain_graph_check::invalid(chain_graph_invariant::stale_dirty_index);
    }

    const auto best{recompute_best_header()};
    if (best.has_value() != m_has_best_header) {
        return chain_graph_check::invalid(chain_graph_invariant::stale_best_header);
    }
    if (best.has_value() && best.assume_value().hash() != m_best_header) {
        return chain_graph_check::invalid(chain_graph_invariant::stale_best_header);
    }

    return chain_graph_check::valid();
}

std::vector<chain_graph_block> block_index_graph::blocks() const
{
    std::vector<chain_graph_block> result;
    result.reserve(m_nodes.size());
    for (const auto& node : m_nodes) {
        result.push_back(block_for(node));
    }
    std::ranges::sort(result, {}, &chain_graph_block::hash);
    return result;
}

block_index_graph::node* block_index_graph::find_node(block_hash hash) noexcept
{
    const auto match{m_node_by_hash.find(hash)};
    if (match == m_node_by_hash.end()) return nullptr;
    if (match->second >= m_nodes.size()) return nullptr;
    if (m_nodes[match->second].hash != hash) return nullptr;
    return &m_nodes[match->second];
}

const block_index_graph::node* block_index_graph::find_node(block_hash hash) const noexcept
{
    const auto match{m_node_by_hash.find(hash)};
    if (match == m_node_by_hash.end()) return nullptr;
    if (match->second >= m_nodes.size()) return nullptr;
    if (m_nodes[match->second].hash != hash) return nullptr;
    return &m_nodes[match->second];
}

bool block_index_graph::branch_failed(const node& candidate) const noexcept
{
    const node* cursor{&candidate};
    while (cursor != nullptr) {
        if (cursor->failed) {
            return true;
        }
        if (is_genesis_parent(cursor->parent_hash)) {
            return false;
        }
        cursor = find_node(cursor->parent_hash);
    }
    return true;
}

bool block_index_graph::eligible_candidate(const node& candidate) const noexcept
{
    const node* cursor{&candidate};
    while (cursor != nullptr) {
        if (cursor->failed || !cursor->valid || !cursor->has_block_data) {
            return false;
        }
        if (is_genesis_parent(cursor->parent_hash)) {
            return true;
        }
        cursor = find_node(cursor->parent_hash);
    }
    return false;
}

bool block_index_graph::unlinked_candidate(const node& candidate) const noexcept
{
    return candidate.valid &&
           candidate.has_block_data &&
           !branch_failed(candidate) &&
           !eligible_candidate(candidate);
}

bool block_index_graph::better_tip(const node& left, const node& right) const noexcept
{
    if (left.work != right.work) {
        return left.work > right.work;
    }
    if (left.height != right.height) {
        return left.height > right.height;
    }
    return left.hash < right.hash;
}

chain_graph_block block_index_graph::block_for(const node& source) const
{
    return chain_graph_block{
        source.hash,
        source.parent_hash,
        source.header,
        source.height,
        source.work,
        source.has_block_data,
        source.valid,
        source.failed,
        source.dirty};
}

std::vector<const block_index_graph::node*> block_index_graph::path_to_root(const node& source) const
{
    std::vector<const node*> result;
    const node* cursor{&source};
    while (cursor != nullptr) {
        result.push_back(cursor);
        if (is_genesis_parent(cursor->parent_hash)) {
            break;
        }
        cursor = find_node(cursor->parent_hash);
    }
    return result;
}

std::vector<block_hash> block_index_graph::recompute_candidates() const
{
    std::vector<const node*> nodes;
    for (const auto& node : m_nodes) {
        if (eligible_candidate(node)) {
            nodes.push_back(&node);
        }
    }
    std::ranges::sort(nodes, [&](const node* left, const node* right) {
        return better_tip(*left, *right);
    });

    std::vector<block_hash> result;
    result.reserve(nodes.size());
    for (const auto* node : nodes) {
        result.push_back(node->hash);
    }
    return result;
}

std::vector<block_hash> block_index_graph::recompute_unlinked() const
{
    std::vector<block_hash> result;
    for (const auto& node : m_nodes) {
        if (unlinked_candidate(node)) {
            result.push_back(node.hash);
        }
    }
    sort_hashes(result);
    return result;
}

std::vector<block_hash> block_index_graph::recompute_failed() const
{
    std::vector<block_hash> result;
    for (const auto& node : m_nodes) {
        if (branch_failed(node)) {
            result.push_back(node.hash);
        }
    }
    sort_hashes(result);
    return result;
}

std::vector<block_hash> block_index_graph::recompute_dirty() const
{
    std::vector<block_hash> result;
    for (const auto& node : m_nodes) {
        if (node.dirty) {
            result.push_back(node.hash);
        }
    }
    sort_hashes(result);
    return result;
}

chain_graph_block_lookup block_index_graph::recompute_best_header() const
{
    const node* best{nullptr};
    for (const auto& node : m_nodes) {
        if (branch_failed(node)) {
            continue;
        }
        if (best == nullptr || better_tip(node, *best)) {
            best = &node;
        }
    }
    return best == nullptr ?
        chain_graph_block_lookup::missing() :
        chain_graph_block_lookup::found(block_for(*best));
}

void block_index_graph::rebuild_indexes()
{
    m_candidates = recompute_candidates();
    m_unlinked = recompute_unlinked();
    m_failed = recompute_failed();
    m_dirty = recompute_dirty();

    const auto best{recompute_best_header()};
    m_has_best_header = best.has_value();
    if (best.has_value()) {
        m_best_header = best.assume_value().hash();
    } else {
        m_best_header = block_hash{};
    }
}

} // namespace bitcoin
