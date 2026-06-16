// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_BLOCK_INDEX_INVARIANTS_H
#define BITCOIN_VALIDATION_CORE_BLOCK_INDEX_INVARIANTS_H

#include <kernel/blockstorage.h>
#include <kernel/cs_main.h>
#include <uint256.h>

#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

class CBlockIndex;
class CChain;

namespace validation {

enum class CoreBlockIndexInvariant {
    PreActiveChainState,
    BestHeader,
    ParentChildGraph,
    Genesis,
    SequenceIds,
    StorageFlags,
    TransactionCounts,
    ChainGeometry,
    SkipPointers,
    ValidityPropagation,
    FailedPropagation,
    CandidateSet,
    UnlinkedBlocks,
    DirtyIndex,
    TraversalCompleteness,
};

struct CoreBlockIndexInvariantViolation {
    CoreBlockIndexInvariant invariant;
    const CBlockIndex* block{nullptr};
    const CBlockIndex* related{nullptr};
    std::string message{};
};

struct CoreBlockIndexInvariantReport {
    std::vector<CoreBlockIndexInvariantViolation> violations{};

    [[nodiscard]] bool ok() const noexcept { return violations.empty(); }
};

struct CoreBlockIndexInvariantView {
    std::span<const CBlockIndex* const> block_indices{};
    CBlockIndex* best_header{nullptr};
    const CChain* active_chain{nullptr};
    const std::set<CBlockIndex*, kernel::CBlockIndexWorkComparator>* candidates{nullptr};
    const std::multimap<CBlockIndex*, CBlockIndex*>* unlinked_blocks{nullptr};
    const std::set<CBlockIndex*>* dirty_block_indices{nullptr};
    uint256 genesis_hash{};
    bool have_pruned{false};
};

[[nodiscard]] CoreBlockIndexInvariantReport CheckCoreBlockIndexInvariants(const CoreBlockIndexInvariantView& view)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

void AssertCoreBlockIndexInvariants(const CoreBlockIndexInvariantView& view)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

} // namespace validation

#endif // BITCOIN_VALIDATION_CORE_BLOCK_INDEX_INVARIANTS_H
