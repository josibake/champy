// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_CHAIN_GRAPH_CHAIN_GRAPH_H
#define BITCOIN_BITCOIN_CHAIN_GRAPH_CHAIN_GRAPH_H

#include <bitcoin/protocol/block_header.h>
#include <bitcoin/protocol/chain_view.h>
#include <bitcoin/protocol/hash.h>

#include <cstddef>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace bitcoin {

enum class graph_update_code {
    applied,
    duplicate_block,
    missing_parent,
    missing_block,
};

class graph_update_result
{
public:
    [[nodiscard]] static constexpr graph_update_result applied() noexcept
    {
        return graph_update_result{graph_update_code::applied};
    }

    [[nodiscard]] static constexpr graph_update_result duplicate_block() noexcept
    {
        return graph_update_result{graph_update_code::duplicate_block};
    }

    [[nodiscard]] static constexpr graph_update_result missing_parent() noexcept
    {
        return graph_update_result{graph_update_code::missing_parent};
    }

    [[nodiscard]] static constexpr graph_update_result missing_block() noexcept
    {
        return graph_update_result{graph_update_code::missing_block};
    }

    [[nodiscard]] constexpr bool changed() const noexcept { return m_code == graph_update_code::applied; }
    [[nodiscard]] constexpr graph_update_code code() const noexcept { return m_code; }

    friend constexpr bool operator==(const graph_update_result&, const graph_update_result&) noexcept = default;

private:
    constexpr explicit graph_update_result(graph_update_code code) noexcept : m_code{code} {}

    graph_update_code m_code;
};

enum class chain_graph_invariant {
    ok,
    duplicate_hash,
    missing_parent,
    height_mismatch,
    stale_candidate_index,
    stale_unlinked_index,
    stale_failed_index,
    stale_dirty_index,
    stale_hash_index,
    stale_best_header,
};

class chain_graph_check
{
public:
    [[nodiscard]] static constexpr chain_graph_check valid() noexcept
    {
        return chain_graph_check{chain_graph_invariant::ok};
    }

    [[nodiscard]] static constexpr chain_graph_check invalid(chain_graph_invariant invariant) noexcept
    {
        return chain_graph_check{invariant};
    }

    [[nodiscard]] constexpr bool ok() const noexcept { return m_invariant == chain_graph_invariant::ok; }
    [[nodiscard]] constexpr chain_graph_invariant failed_invariant() const noexcept { return m_invariant; }

    friend constexpr bool operator==(const chain_graph_check&, const chain_graph_check&) noexcept = default;

private:
    constexpr explicit chain_graph_check(chain_graph_invariant invariant) noexcept : m_invariant{invariant} {}

    chain_graph_invariant m_invariant;
};

enum class reorg_plan_code {
    planned,
    missing_block,
    ineligible_candidate,
};

class reorg_plan
{
public:
    [[nodiscard]] std::span<const block_hash> disconnects() const noexcept { return m_disconnects; }
    [[nodiscard]] std::span<const block_hash> connects() const noexcept { return m_connects; }
    [[nodiscard]] bool empty() const noexcept { return m_disconnects.empty() && m_connects.empty(); }

    friend bool operator==(const reorg_plan&, const reorg_plan&) noexcept = default;

private:
    friend class block_index_graph;
    friend class reorg_plan_result;

    reorg_plan(std::vector<block_hash> disconnects, std::vector<block_hash> connects) :
        m_disconnects{std::move(disconnects)},
        m_connects{std::move(connects)}
    {
    }

    std::vector<block_hash> m_disconnects;
    std::vector<block_hash> m_connects;
};

class reorg_plan_result
{
public:
    [[nodiscard]] static reorg_plan_result planned(reorg_plan plan)
    {
        return reorg_plan_result{std::move(plan)};
    }

    [[nodiscard]] static reorg_plan_result missing_block()
    {
        return reorg_plan_result{reorg_plan_code::missing_block};
    }

    [[nodiscard]] static reorg_plan_result ineligible_candidate()
    {
        return reorg_plan_result{reorg_plan_code::ineligible_candidate};
    }

    [[nodiscard]] bool has_plan() const noexcept { return m_code == reorg_plan_code::planned; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_plan(); }
    [[nodiscard]] reorg_plan_code code() const noexcept { return m_code; }
    [[nodiscard]] const reorg_plan& assume_plan() const& noexcept { return m_plan; }

private:
    explicit reorg_plan_result(reorg_plan plan) :
        m_code{reorg_plan_code::planned},
        m_plan{std::move(plan)}
    {
    }

    explicit reorg_plan_result(reorg_plan_code code) :
        m_code{code},
        m_plan{{}, {}}
    {
    }

    reorg_plan_code m_code;
    reorg_plan m_plan;
};

class chain_graph_block
{
public:
    [[nodiscard]] block_hash hash() const noexcept { return m_hash; }
    [[nodiscard]] block_hash parent_hash() const noexcept { return m_parent_hash; }
    [[nodiscard]] const block_header& header() const noexcept { return m_header; }
    [[nodiscard]] block_height height() const noexcept { return m_height; }
    [[nodiscard]] chain_work work() const noexcept { return m_work; }
    [[nodiscard]] bool has_block_data() const noexcept { return m_has_block_data; }
    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    [[nodiscard]] bool explicitly_failed() const noexcept { return m_failed; }
    [[nodiscard]] bool dirty() const noexcept { return m_dirty; }

    friend bool operator==(const chain_graph_block&, const chain_graph_block&) noexcept = default;

private:
    friend class block_index_graph;
    friend class chain_graph_block_lookup;

    chain_graph_block(
        block_hash hash,
        block_hash parent_hash,
        block_header header,
        block_height height,
        chain_work work,
        bool has_block_data,
        bool valid,
        bool failed,
        bool dirty) :
        m_hash{hash},
        m_parent_hash{parent_hash},
        m_header{header},
        m_height{height},
        m_work{work},
        m_has_block_data{has_block_data},
        m_valid{valid},
        m_failed{failed},
        m_dirty{dirty}
    {
    }

    block_hash m_hash;
    block_hash m_parent_hash;
    block_header m_header;
    block_height m_height;
    chain_work m_work;
    bool m_has_block_data{false};
    bool m_valid{false};
    bool m_failed{false};
    bool m_dirty{false};
};

class chain_graph_block_lookup
{
public:
    [[nodiscard]] static chain_graph_block_lookup missing()
    {
        return chain_graph_block_lookup{};
    }

    [[nodiscard]] static chain_graph_block_lookup found(chain_graph_block block)
    {
        return chain_graph_block_lookup{true, std::move(block)};
    }

    [[nodiscard]] bool has_value() const noexcept { return m_found; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] const chain_graph_block& assume_value() const& noexcept { return m_block; }

private:
    chain_graph_block_lookup() = default;
    chain_graph_block_lookup(bool found, chain_graph_block block) :
        m_found{found}, m_block{std::move(block)}
    {
    }

    bool m_found{false};
    chain_graph_block m_block{
        block_hash{},
        block_hash{},
        block_header{},
        block_height{},
        chain_work{},
        false,
        false,
        false,
        false};
};

class active_chain
{
public:
    active_chain() noexcept = default;

    [[nodiscard]] bool empty() const noexcept { return m_headers.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_headers.size(); }
    [[nodiscard]] std::span<const block_header> headers() const noexcept { return m_headers; }
    [[nodiscard]] std::span<const block_hash> hashes() const noexcept { return m_hashes; }
    [[nodiscard]] const block_header& operator[](std::size_t index) const noexcept { return m_headers[index]; }

private:
    friend class block_index_graph;

    active_chain(std::vector<block_hash> hashes, std::vector<block_header> headers) :
        m_hashes{std::move(hashes)},
        m_headers{std::move(headers)}
    {
    }

    std::vector<block_hash> m_hashes;
    std::vector<block_header> m_headers;
};

class block_index_graph
{
public:
    [[nodiscard]] graph_update_result add_header(const block_header& header, chain_work work);
    [[nodiscard]] graph_update_result add_block_data(block_hash hash);
    [[nodiscard]] graph_update_result mark_valid(block_hash hash);
    [[nodiscard]] graph_update_result invalidate(block_hash hash);
    [[nodiscard]] graph_update_result reconsider(block_hash hash);
    [[nodiscard]] graph_update_result mark_clean(block_hash hash);

    [[nodiscard]] chain_graph_block_lookup find(block_hash hash) const;
    [[nodiscard]] chain_graph_block_lookup best_header() const;
    [[nodiscard]] reorg_plan_result plan_reorg_to(block_hash target) const;
    [[nodiscard]] active_chain active() const;
    [[nodiscard]] chain_graph_check check_invariants() const;

    [[nodiscard]] std::vector<chain_graph_block> blocks() const;
    [[nodiscard]] std::span<const block_hash> candidates() const noexcept { return m_candidates; }
    [[nodiscard]] std::span<const block_hash> unlinked_blocks() const noexcept { return m_unlinked; }
    [[nodiscard]] std::span<const block_hash> failed_blocks() const noexcept { return m_failed; }
    [[nodiscard]] std::span<const block_hash> dirty_blocks() const noexcept { return m_dirty; }

private:
    struct node {
        block_hash hash;
        block_hash parent_hash;
        block_header header;
        block_height height;
        chain_work work;
        bool has_block_data{false};
        bool valid{false};
        bool failed{false};
        bool dirty{true};
    };

    [[nodiscard]] node* find_node(block_hash hash) noexcept;
    [[nodiscard]] const node* find_node(block_hash hash) const noexcept;
    [[nodiscard]] bool branch_failed(const node& candidate) const noexcept;
    [[nodiscard]] bool eligible_candidate(const node& candidate) const noexcept;
    [[nodiscard]] bool unlinked_candidate(const node& candidate) const noexcept;
    [[nodiscard]] bool better_tip(const node& left, const node& right) const noexcept;
    [[nodiscard]] chain_graph_block block_for(const node& source) const;
    [[nodiscard]] std::vector<const node*> path_to_root(const node& source) const;
    [[nodiscard]] std::vector<block_hash> recompute_candidates() const;
    [[nodiscard]] std::vector<block_hash> recompute_unlinked() const;
    [[nodiscard]] std::vector<block_hash> recompute_failed() const;
    [[nodiscard]] std::vector<block_hash> recompute_dirty() const;
    [[nodiscard]] chain_graph_block_lookup recompute_best_header() const;
    void rebuild_indexes();

    std::vector<node> m_nodes;
    std::map<block_hash, std::size_t> m_node_by_hash;
    std::vector<block_hash> m_candidates;
    std::vector<block_hash> m_unlinked;
    std::vector<block_hash> m_failed;
    std::vector<block_hash> m_dirty;
    bool m_has_best_header{false};
    block_hash m_best_header;
};

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_CHAIN_GRAPH_CHAIN_GRAPH_H
