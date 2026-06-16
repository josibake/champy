// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <kernel/cs_main.h>
#include <sync.h>
#include <uint256.h>
#include <validation/core_block_index_invariants.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace {

CBlockHeader Header(uint8_t nonce)
{
    CBlockHeader header;
    header.nVersion = 1;
    header.nTime = nonce;
    header.nBits = 1;
    header.nNonce = nonce;
    return header;
}

struct SyntheticBlockIndex {
    uint256 hash;
    CBlockIndex index;

    SyntheticBlockIndex(uint8_t id, CBlockIndex* previous = nullptr)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
        : hash{id}, index{Header(id)}
    {
        index.phashBlock = &hash;
        index.pprev = previous;
        index.nHeight = previous ? previous->nHeight + 1 : 0;
        index.nChainWork = previous ? previous->nChainWork + arith_uint256{1} : arith_uint256{1};
        index.nStatus = BLOCK_VALID_TRANSACTIONS | BLOCK_HAVE_DATA;
        index.nTx = 1;
        index.m_chain_tx_count = previous ? previous->m_chain_tx_count + 1 : 1;
        index.nSequenceId = SEQ_ID_INIT_FROM_DISK;
        index.BuildSkip();
    }
};

struct SyntheticBlockTree {
    std::vector<std::unique_ptr<SyntheticBlockIndex>> entries;

    SyntheticBlockIndex& Add(uint8_t id, CBlockIndex* previous = nullptr)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        entries.push_back(std::make_unique<SyntheticBlockIndex>(id, previous));
        return *entries.back();
    }

    std::vector<const CBlockIndex*> Snapshot() const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        std::vector<const CBlockIndex*> snapshot;
        snapshot.reserve(entries.size());
        for (const auto& entry : entries) {
            snapshot.push_back(&entry->index);
        }
        return snapshot;
    }
};

bool ContainsInvariant(
    const validation::CoreBlockIndexInvariantReport& report,
    validation::CoreBlockIndexInvariant invariant)
{
    for (const auto& violation : report.violations) {
        if (violation.invariant == invariant) return true;
    }
    return false;
}

validation::CoreBlockIndexInvariantReport Check(
    const std::vector<const CBlockIndex*>& snapshot,
    CBlockIndex* best_header,
    CChain& active_chain,
    const std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator>& candidates,
    const std::multimap<CBlockIndex*, CBlockIndex*>& unlinked_blocks,
    const uint256& genesis_hash,
    const std::set<CBlockIndex*>* dirty_block_indices = nullptr)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    static const std::set<CBlockIndex*> EMPTY_DIRTY_BLOCK_INDICES;
    return validation::CheckCoreBlockIndexInvariants({
        .block_indices = std::span<const CBlockIndex* const>{snapshot},
        .best_header = best_header,
        .active_chain = &active_chain,
        .candidates = &candidates,
        .unlinked_blocks = &unlinked_blocks,
        .dirty_block_indices = dirty_block_indices ? dirty_block_indices : &EMPTY_DIRTY_BLOCK_INDICES,
        .genesis_hash = genesis_hash,
        .have_pruned = false,
    });
}

} // namespace

BOOST_AUTO_TEST_SUITE(block_index_invariant_tests)

BOOST_AUTO_TEST_CASE(valid_two_block_chain_has_no_violations)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&child.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(report.ok());
}

BOOST_AUTO_TEST_CASE(pre_active_chain_rejects_multiple_block_indices)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    tree.Add(2, &genesis.index);
    CChain inactive_chain;
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates;
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, nullptr, inactive_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::PreActiveChainState));
}

BOOST_AUTO_TEST_CASE(wrong_genesis_hash_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&child.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, uint256{99})};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::Genesis));
}

BOOST_AUTO_TEST_CASE(failed_best_header_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    child.index.nStatus |= BLOCK_FAILED_VALID;
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates;
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::BestHeader));
}

BOOST_AUTO_TEST_CASE(parent_child_graph_rejects_null_snapshot_entry)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    CChain active_chain;
    active_chain.SetTip(genesis.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&genesis.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    auto snapshot{tree.Snapshot()};
    snapshot.push_back(nullptr);
    const auto report{Check(snapshot, &genesis.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::ParentChildGraph));
}

BOOST_AUTO_TEST_CASE(undo_without_block_data_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    child.index.nStatus = BLOCK_VALID_TRANSACTIONS | BLOCK_HAVE_UNDO;
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&child.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::StorageFlags));
}

BOOST_AUTO_TEST_CASE(unlinked_sequence_id_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    child.index.nTx = 0;
    child.index.m_chain_tx_count = 0;
    child.index.nSequenceId = SEQ_ID_INIT_FROM_DISK + 1;
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates;
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::SequenceIds));
}

BOOST_AUTO_TEST_CASE(bad_chain_transaction_count_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    child.index.m_chain_tx_count = 99;
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&child.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::TransactionCounts));
}

BOOST_AUTO_TEST_CASE(bad_height_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    child.index.nHeight = 3;
    CChain active_chain;
    active_chain.SetTip(genesis.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&genesis.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &genesis.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::ChainGeometry));
}

BOOST_AUTO_TEST_CASE(missing_skip_pointer_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    auto& grandchild{tree.Add(3, &child.index)};
    grandchild.index.pskip = nullptr;
    CChain active_chain;
    active_chain.SetTip(grandchild.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&grandchild.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &grandchild.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::SkipPointers));
}

BOOST_AUTO_TEST_CASE(non_tree_valid_ancestor_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    child.index.nStatus = BLOCK_VALID_RESERVED | BLOCK_HAVE_DATA;
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates;
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::ValidityPropagation));
}

BOOST_AUTO_TEST_CASE(failed_ancestor_requires_failed_descendant)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    genesis.index.nStatus |= BLOCK_FAILED_VALID;
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&child.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::FailedPropagation));
}

BOOST_AUTO_TEST_CASE(eligible_tip_missing_from_candidate_set_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates;
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::CandidateSet));
}

BOOST_AUTO_TEST_CASE(block_without_data_in_unlinked_set_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    child.index.nStatus = BLOCK_VALID_TREE;
    child.index.nTx = 0;
    child.index.m_chain_tx_count = 0;
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates;
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks{{&genesis.index, &child.index}};

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::UnlinkedBlocks));
}

BOOST_AUTO_TEST_CASE(dirty_index_outside_snapshot_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    CChain active_chain;
    active_chain.SetTip(genesis.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&genesis.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;
    SyntheticBlockIndex external{2, &genesis.index};
    std::set<CBlockIndex*> dirty_block_indices{&external.index};

    const auto snapshot{tree.Snapshot()};
    const auto report{Check(snapshot, &genesis.index, active_chain, candidates, unlinked_blocks, genesis.hash, &dirty_block_indices)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::DirtyIndex));
}

BOOST_AUTO_TEST_CASE(snapshot_missing_best_header_ancestor_is_reported)
{
    LOCK(::cs_main);
    SyntheticBlockTree tree;
    auto& genesis{tree.Add(1)};
    auto& child{tree.Add(2, &genesis.index)};
    CChain active_chain;
    active_chain.SetTip(child.index);
    std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator> candidates{&child.index};
    std::multimap<CBlockIndex*, CBlockIndex*> unlinked_blocks;

    std::vector<const CBlockIndex*> snapshot{&child.index};
    const auto report{Check(snapshot, &child.index, active_chain, candidates, unlinked_blocks, genesis.hash)};

    BOOST_CHECK(ContainsInvariant(report, validation::CoreBlockIndexInvariant::TraversalCompleteness));
}

BOOST_AUTO_TEST_SUITE_END()
