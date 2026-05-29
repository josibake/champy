// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/sequence_locks_adapters.h>

#include <chain.h>
#include <consensus/consensus.h>
#include <primitives/transaction.h>
#include <util/check.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

bool EnforceBIP68SequenceLocks(const CTransaction& tx, int flags)
{
    return tx.version >= 2 && flags & LOCKTIME_VERIFY_SEQUENCE;
}

std::vector<Consensus::SequenceLockInputContext> BuildSequenceLockInputContext(const CTransaction& tx, int flags, std::span<const int> prev_heights, const CBlockIndex& block)
{
    const bool enforce_sequence_locks{EnforceBIP68SequenceLocks(tx, flags)};
    std::vector<Consensus::SequenceLockInputContext> input_contexts;
    input_contexts.reserve(prev_heights.size());

    for (size_t txin_index{0}; txin_index < tx.vin.size(); ++txin_index) {
        const CTxIn& txin{tx.vin[txin_index]};
        const int coin_height{prev_heights[txin_index]};
        int64_t previous_median_time_past{0};
        if (enforce_sequence_locks &&
            !(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) &&
            (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG)) {
            previous_median_time_past = Assert(block.GetAncestor(std::max(coin_height - 1, 0)))->GetMedianTimePast();
        }
        input_contexts.push_back({
            .height = coin_height,
            .previous_median_time_past = previous_median_time_past,
        });
    }

    return input_contexts;
}

} // namespace

std::pair<int, int64_t> Consensus::CalculateSequenceLocksAtBlock(const CTransaction& tx, int flags, std::span<const int> prev_heights, const CBlockIndex& block)
{
    const std::vector<SequenceLockInputContext> input_contexts{BuildSequenceLockInputContext(tx, flags, prev_heights, block)};
    return Consensus::CalculateSequenceLocks(tx, flags, SequenceLockContext{
        .block_height = block.nHeight,
        .previous_median_time_past = block.pprev ? block.pprev->GetMedianTimePast() : 0,
        .inputs = input_contexts,
    });
}

std::pair<int, int64_t> Consensus::CalculateSequenceLocks(const CTransaction& tx, int flags, std::span<const int> prev_heights, const CBlockIndex& block)
{
    return Consensus::CalculateSequenceLocksAtBlock(tx, flags, prev_heights, block);
}

bool Consensus::EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lock_pair)
{
    assert(block.pprev);
    return Consensus::EvaluateSequenceLocksAtBlock(block.nHeight, block.pprev->GetMedianTimePast(), lock_pair);
}

bool Consensus::SequenceLocks(const CTransaction& tx, int flags, std::span<const int> prev_heights, const CBlockIndex& block)
{
    return Consensus::EvaluateSequenceLocks(block, Consensus::CalculateSequenceLocks(tx, flags, prev_heights, block));
}
