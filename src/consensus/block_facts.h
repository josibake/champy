// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_BLOCK_FACTS_H
#define BITCOIN_CONSENSUS_BLOCK_FACTS_H

#include <primitives/transaction.h>
#include <uint256.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

class CBlock;

namespace Consensus {

/**
 * Immutable structure-only facts computed from a block.
 *
 * These values are observations about the block data. They do not decide
 * validity by themselves and they do not update the block's mutable validation
 * caches.
 */
struct BlockStructuralFacts {
    std::size_t transaction_count{0};
    uint256 merkle_root;
    bool merkle_mutated{false};
    int64_t stripped_size{0};
};

/**
 * Immutable facts computed from a block for one validation attempt.
 */
struct BlockFacts {
    BlockStructuralFacts structure;
    uint256 witness_merkle_root;
    bool has_witness{false};
    std::optional<int> witness_commitment_index;
    int64_t weight{0};
};

[[nodiscard]] BlockStructuralFacts ComputeBlockStructuralFacts(std::span<const CTransactionRef> transactions, int64_t stripped_size);
[[nodiscard]] BlockStructuralFacts ComputeBlockStructuralFacts(const CBlock& block);
[[nodiscard]] BlockFacts ComputeBlockFacts(std::span<const CTransactionRef> transactions, int64_t stripped_size, int64_t weight);
[[nodiscard]] BlockFacts ComputeBlockFacts(const CBlock& block);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_BLOCK_FACTS_H
