// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SEQUENCE_LOCKS_H
#define BITCOIN_CONSENSUS_SEQUENCE_LOCKS_H

#include <cstdint>
#include <span>
#include <utility>

class CTransaction;

namespace Consensus {

struct SequenceLockInputContext {
    int height{0};
    int64_t previous_median_time_past{0};
};

struct SequenceLockContext {
    int block_height{0};
    int64_t previous_median_time_past{0};
    std::span<const SequenceLockInputContext> inputs;
};

[[nodiscard]] std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction& tx, int flags, const SequenceLockContext& context);
[[nodiscard]] bool EvaluateSequenceLocks(const SequenceLockContext& context, std::pair<int, int64_t> lock_pair);
[[nodiscard]] bool EvaluateSequenceLocksAtBlock(int block_height, int64_t previous_median_time_past, std::pair<int, int64_t> lock_pair);
[[nodiscard]] bool SequenceLocks(const CTransaction& tx, int flags, const SequenceLockContext& context);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SEQUENCE_LOCKS_H
