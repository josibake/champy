// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_index_invariants.h>

#include <chain.h>

#include <cassert>
#include <algorithm>
#include <string>
#include <utility>

namespace validation {

namespace {

void RecordViolation(
    CoreBlockIndexInvariantReport& report,
    CoreBlockIndexInvariant invariant,
    const CBlockIndex* block,
    const CBlockIndex* related,
    std::string message)
{
    report.violations.push_back({
        .invariant = invariant,
        .block = block,
        .related = related,
        .message = std::move(message),
    });
}

} // namespace

CoreBlockIndexInvariantReport CheckCoreBlockIndexInvariants(const CoreBlockIndexInvariantView& view)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    CoreBlockIndexInvariantReport report;
    if (!view.active_chain) {
        RecordViolation(report, CoreBlockIndexInvariant::PreActiveChainState, nullptr, nullptr, "missing active chain view");
        return report;
    }
    if (!view.unlinked_blocks) {
        RecordViolation(report, CoreBlockIndexInvariant::UnlinkedBlocks, nullptr, nullptr, "missing unlinked-block view");
        return report;
    }
    if (!view.dirty_block_indices) {
        RecordViolation(report, CoreBlockIndexInvariant::DirtyIndex, nullptr, nullptr, "missing dirty-index view");
        return report;
    }
    if (view.active_chain->Height() < 0) {
        if (view.block_indices.size() > 1) {
            RecordViolation(report, CoreBlockIndexInvariant::PreActiveChainState, nullptr, nullptr, "pre-active chain block index contains more than genesis");
        }
        return report;
    }
    if (!view.best_header) {
        RecordViolation(report, CoreBlockIndexInvariant::BestHeader, nullptr, nullptr, "missing best header");
        return report;
    }
    if (view.best_header->nStatus & BLOCK_FAILED_VALID) {
        RecordViolation(report, CoreBlockIndexInvariant::BestHeader, view.best_header, nullptr, "best header is failed");
    }

    CChain best_hdr_chain;
    best_hdr_chain.SetTip(*view.best_header);

    for (const CBlockIndex* dirty_index : *view.dirty_block_indices) {
        if (!dirty_index) {
            RecordViolation(report, CoreBlockIndexInvariant::DirtyIndex, nullptr, nullptr, "dirty-index set contains null");
            continue;
        }
        if (std::ranges::find(view.block_indices, dirty_index) == view.block_indices.end()) {
            RecordViolation(report, CoreBlockIndexInvariant::DirtyIndex, dirty_index, nullptr, "dirty-index entry is outside the block-index snapshot");
        }
    }

    std::multimap<const CBlockIndex*, const CBlockIndex*> forward;
    for (const CBlockIndex* block_index : view.block_indices) {
        if (!block_index) {
            RecordViolation(report, CoreBlockIndexInvariant::ParentChildGraph, nullptr, nullptr, "block-index snapshot contains null");
            continue;
        }
        if (!best_hdr_chain.Contains(*block_index)) {
            if (!block_index->pprev) {
                RecordViolation(report, CoreBlockIndexInvariant::ParentChildGraph, block_index, nullptr, "non-best-header block has no parent");
                continue;
            }
            forward.emplace(block_index->pprev, block_index);
        }
    }
    if (forward.size() + best_hdr_chain.Height() + 1 != view.block_indices.size()) {
        RecordViolation(report, CoreBlockIndexInvariant::TraversalCompleteness, nullptr, nullptr, "best-header chain and forward graph do not cover the snapshot");
    }

    const CBlockIndex* pindex = best_hdr_chain[0];
    if (!pindex) {
        RecordViolation(report, CoreBlockIndexInvariant::BestHeader, view.best_header, nullptr, "best-header chain has no genesis");
        return report;
    }

    size_t nNodes = 0;
    int nHeight = 0;
    const CBlockIndex* pindexFirstInvalid = nullptr;
    const CBlockIndex* pindexFirstMissing = nullptr;
    const CBlockIndex* pindexFirstNeverProcessed = nullptr;
    const CBlockIndex* pindexFirstNotTreeValid = nullptr;
    const CBlockIndex* pindexFirstNotTransactionsValid = nullptr;
    const CBlockIndex* pindexFirstNotChainValid = nullptr;
    const CBlockIndex* pindexFirstNotScriptsValid = nullptr;

    bool traversal_broken{false};
    while (pindex != nullptr && !traversal_broken) {
        nNodes++;
        if (nNodes > view.block_indices.size() + 1) {
            RecordViolation(report, CoreBlockIndexInvariant::TraversalCompleteness, pindex, nullptr, "block-index traversal did not terminate");
            break;
        }
        if (pindexFirstInvalid == nullptr && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == nullptr && !(pindex->nStatus & BLOCK_HAVE_DATA)) {
            pindexFirstMissing = pindex;
        }
        if (pindexFirstNeverProcessed == nullptr && pindex->nTx == 0) pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;

        if (pindex->pprev != nullptr) {
            if (pindexFirstNotTransactionsValid == nullptr &&
                    (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) {
                pindexFirstNotTransactionsValid = pindex;
            }
            if (pindexFirstNotChainValid == nullptr &&
                    (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) {
                pindexFirstNotChainValid = pindex;
            }
            if (pindexFirstNotScriptsValid == nullptr &&
                    (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) {
                pindexFirstNotScriptsValid = pindex;
            }
        }

        if (pindex->pprev == nullptr) {
            if (!pindex->phashBlock) {
                RecordViolation(report, CoreBlockIndexInvariant::Genesis, pindex, nullptr, "genesis block has no hash witness");
            } else if (pindex->GetBlockHash() != view.genesis_hash) {
                RecordViolation(report, CoreBlockIndexInvariant::Genesis, pindex, nullptr, "genesis hash does not match consensus genesis");
            }
            if (view.active_chain->Genesis() != nullptr && pindex != view.active_chain->Genesis()) {
                RecordViolation(report, CoreBlockIndexInvariant::Genesis, pindex, view.active_chain->Genesis(), "active-chain genesis is not the block-index genesis");
            }
        }
        if (!pindex->HaveNumChainTxs() && pindex->nSequenceId > SEQ_ID_INIT_FROM_DISK) {
            RecordViolation(report, CoreBlockIndexInvariant::SequenceIds, pindex, nullptr, "unlinked block has advanced sequence id");
        }
        if (!view.have_pruned) {
            if (!(pindex->nStatus & BLOCK_HAVE_DATA) != (pindex->nTx == 0)) {
                RecordViolation(report, CoreBlockIndexInvariant::StorageFlags, pindex, nullptr, "HAVE_DATA and transaction count disagree on an unpruned node");
            }
            if (pindexFirstMissing != pindexFirstNeverProcessed) {
                RecordViolation(report, CoreBlockIndexInvariant::StorageFlags, pindex, pindexFirstMissing, "first missing data does not match first never-processed ancestor");
            }
        } else if ((pindex->nStatus & BLOCK_HAVE_DATA) && pindex->nTx == 0) {
            RecordViolation(report, CoreBlockIndexInvariant::StorageFlags, pindex, nullptr, "pruned node with data has no transaction count");
        }
        if ((pindex->nStatus & BLOCK_HAVE_UNDO) && !(pindex->nStatus & BLOCK_HAVE_DATA)) {
            RecordViolation(report, CoreBlockIndexInvariant::StorageFlags, pindex, nullptr, "UNDO is present without block data");
        }
        if (((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) != (pindex->nTx > 0)) {
            RecordViolation(report, CoreBlockIndexInvariant::TransactionCounts, pindex, nullptr, "VALID_TRANSACTIONS and transaction count disagree");
        }
        if ((pindexFirstNeverProcessed == nullptr) != pindex->HaveNumChainTxs()) {
            RecordViolation(report, CoreBlockIndexInvariant::TransactionCounts, pindex, pindexFirstNeverProcessed, "HaveNumChainTxs disagrees with first never-processed ancestor");
        }
        if ((pindexFirstNotTransactionsValid == nullptr) != pindex->HaveNumChainTxs()) {
            RecordViolation(report, CoreBlockIndexInvariant::TransactionCounts, pindex, pindexFirstNotTransactionsValid, "HaveNumChainTxs disagrees with transaction-valid ancestors");
        }
        if (pindex->nHeight != nHeight) {
            RecordViolation(report, CoreBlockIndexInvariant::ChainGeometry, pindex, nullptr, "height does not match traversal height");
        }
        if (pindex->pprev != nullptr && pindex->nChainWork < pindex->pprev->nChainWork) {
            RecordViolation(report, CoreBlockIndexInvariant::ChainGeometry, pindex, pindex->pprev, "chain work is below parent chain work");
        }
        if (nHeight >= 2 && (!pindex->pskip || pindex->pskip->nHeight >= nHeight)) {
            RecordViolation(report, CoreBlockIndexInvariant::SkipPointers, pindex, pindex->pskip, "skip pointer is missing or does not point backward");
        }
        if (pindexFirstNotTreeValid != nullptr) {
            RecordViolation(report, CoreBlockIndexInvariant::ValidityPropagation, pindex, pindexFirstNotTreeValid, "block-index entry has a non-tree-valid ancestor");
        }
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN && pindexFirstNotChainValid != nullptr) {
            RecordViolation(report, CoreBlockIndexInvariant::ValidityPropagation, pindex, pindexFirstNotChainValid, "CHAIN-valid block has a non-CHAIN-valid ancestor");
        }
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS && pindexFirstNotScriptsValid != nullptr) {
            RecordViolation(report, CoreBlockIndexInvariant::ValidityPropagation, pindex, pindexFirstNotScriptsValid, "SCRIPTS-valid block has a non-SCRIPTS-valid ancestor");
        }
        if (pindexFirstInvalid == nullptr) {
            if (pindex->nStatus & BLOCK_FAILED_VALID) {
                RecordViolation(report, CoreBlockIndexInvariant::FailedPropagation, pindex, nullptr, "failed flag set without an invalid ancestor");
            }
        } else if (!(pindex->nStatus & BLOCK_FAILED_VALID)) {
            RecordViolation(report, CoreBlockIndexInvariant::FailedPropagation, pindex, pindexFirstInvalid, "invalid descendant is not marked failed");
        }
        if (!pindex->pprev) {
            if (pindex->m_chain_tx_count != pindex->nTx) {
                RecordViolation(report, CoreBlockIndexInvariant::TransactionCounts, pindex, nullptr, "genesis chain transaction count does not equal block transaction count");
            }
        } else if (pindex->pprev->m_chain_tx_count > 0 && pindex->nTx > 0) {
            if (pindex->m_chain_tx_count != pindex->nTx + pindex->pprev->m_chain_tx_count) {
                RecordViolation(report, CoreBlockIndexInvariant::TransactionCounts, pindex, pindex->pprev, "chain transaction count sum is wrong");
            }
        } else if (pindex->m_chain_tx_count != 0) {
            RecordViolation(report, CoreBlockIndexInvariant::TransactionCounts, pindex, nullptr, "chain transaction count is set without complete ancestors");
        }
        if (!(pindex->nStatus & BLOCK_FAILED_VALID) && pindex->nChainWork > view.best_header->nChainWork) {
            RecordViolation(report, CoreBlockIndexInvariant::BestHeader, pindex, view.best_header, "non-failed block has more work than best header");
        }

        if (view.active_chain->Tip() != nullptr) {
            if (!view.candidates) {
                RecordViolation(report, CoreBlockIndexInvariant::CandidateSet, pindex, nullptr, "missing candidate-set view");
            } else {
                const CChain& active_chain{*view.active_chain};
                const auto& candidates{*view.candidates};
                if (!kernel::CBlockIndexWorkComparator()(pindex, active_chain.Tip()) && pindexFirstNeverProcessed == nullptr) {
                    if (pindexFirstInvalid == nullptr && (pindexFirstMissing == nullptr || pindex == active_chain.Tip())) {
                        if (!candidates.contains(pindex)) {
                            RecordViolation(report, CoreBlockIndexInvariant::CandidateSet, pindex, active_chain.Tip(), "eligible block is absent from candidate set");
                        }
                    }
                } else if (candidates.contains(pindex)) {
                    RecordViolation(report, CoreBlockIndexInvariant::CandidateSet, pindex, active_chain.Tip(), "ineligible block is present in candidate set");
                }
            }
        }

        auto rangeUnlinked{view.unlinked_blocks->equal_range(pindex->pprev)};
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            if (rangeUnlinked.first->first != pindex->pprev) {
                RecordViolation(report, CoreBlockIndexInvariant::UnlinkedBlocks, pindex, rangeUnlinked.first->first, "unlinked range has wrong parent key");
            }
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != nullptr && pindexFirstInvalid == nullptr && !foundInUnlinked) {
            RecordViolation(report, CoreBlockIndexInvariant::UnlinkedBlocks, pindex, pindexFirstNeverProcessed, "block with data and never-processed ancestor is absent from unlinked set");
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA) && foundInUnlinked) {
            RecordViolation(report, CoreBlockIndexInvariant::UnlinkedBlocks, pindex, nullptr, "block without data is present in unlinked set");
        }
        if (pindexFirstMissing == nullptr && foundInUnlinked) {
            RecordViolation(report, CoreBlockIndexInvariant::UnlinkedBlocks, pindex, nullptr, "block is unlinked without missing parent data");
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == nullptr && pindexFirstMissing != nullptr) {
            if (!view.have_pruned) {
                RecordViolation(report, CoreBlockIndexInvariant::UnlinkedBlocks, pindex, pindexFirstMissing, "missing previously-processed parent requires pruning");
            }
            if (view.active_chain->Tip() != nullptr && view.candidates) {
                const CChain& active_chain{*view.active_chain};
                const auto& candidates{*view.candidates};
                if (!kernel::CBlockIndexWorkComparator()(pindex, active_chain.Tip()) && !candidates.contains(pindex) && pindexFirstInvalid == nullptr && !foundInUnlinked) {
                    RecordViolation(report, CoreBlockIndexInvariant::UnlinkedBlocks, pindex, active_chain.Tip(), "better missing-data block is absent from unlinked set");
                }
            }
        }

        auto range{forward.equal_range(pindex)};
        if (range.first != range.second) {
            pindex = range.first->second;
            nHeight++;
            continue;
        } else if (best_hdr_chain.Contains(*pindex)) {
            nHeight++;
            pindex = best_hdr_chain[nHeight];
            if (!pindex) break;
            continue;
        }
        while (pindex) {
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = nullptr;
            if (pindex == pindexFirstMissing) pindexFirstMissing = nullptr;
            if (pindex == pindexFirstNeverProcessed) pindexFirstNeverProcessed = nullptr;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = nullptr;
            if (pindex == pindexFirstNotTransactionsValid) pindexFirstNotTransactionsValid = nullptr;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = nullptr;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = nullptr;
            CBlockIndex* pindexPar = pindex->pprev;
            auto rangePar{forward.equal_range(pindexPar)};
            while (rangePar.first != rangePar.second && rangePar.first->second != pindex) {
                rangePar.first++;
            }
            if (rangePar.first == rangePar.second) {
                RecordViolation(report, CoreBlockIndexInvariant::TraversalCompleteness, pindex, pindexPar, "parent forward range does not contain current block");
                traversal_broken = true;
                break;
            }
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                pindex = rangePar.first->second;
                break;
            } else if (nHeight > 0 && pindexPar == best_hdr_chain[nHeight - 1]) {
                pindex = best_hdr_chain[nHeight];
                if ((pindex == nullptr) != (pindexPar == best_hdr_chain.Tip())) {
                    RecordViolation(report, CoreBlockIndexInvariant::TraversalCompleteness, pindexPar, best_hdr_chain.Tip(), "best-header sibling traversal ended inconsistently");
                }
                break;
            } else {
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    if (nNodes != forward.size() + best_hdr_chain.Height() + 1) {
        RecordViolation(report, CoreBlockIndexInvariant::TraversalCompleteness, nullptr, nullptr, "traversed node count does not match graph size");
    }
    return report;
}

void AssertCoreBlockIndexInvariants(const CoreBlockIndexInvariantView& view)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    const auto report{CheckCoreBlockIndexInvariants(view)};
    assert(report.ok());
}

} // namespace validation
