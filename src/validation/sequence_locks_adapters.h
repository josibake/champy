// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SEQUENCE_LOCKS_ADAPTERS_H
#define BITCOIN_SEQUENCE_LOCKS_ADAPTERS_H

#include <consensus/sequence_locks.h>

#include <span>
#include <utility>

class CBlockIndex;
class CTransaction;

namespace validation {

[[nodiscard]] std::pair<int, int64_t> CalculateSequenceLocksAtBlock(const CTransaction& tx, int flags, std::span<const int> prev_heights, const CBlockIndex& block);
[[nodiscard]] std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction& tx, int flags, std::span<const int> prev_heights, const CBlockIndex& block);
[[nodiscard]] bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lock_pair);
[[nodiscard]] bool SequenceLocks(const CTransaction& tx, int flags, std::span<const int> prev_heights, const CBlockIndex& block);

} // namespace validation

#endif // BITCOIN_SEQUENCE_LOCKS_ADAPTERS_H
