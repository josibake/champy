// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_facts.h>

#include <consensus/merkle.h>
#include <consensus/predicates.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <algorithm>

namespace Consensus {

BlockStructuralFacts ComputeBlockStructuralFacts(std::span<const CTransactionRef> transactions, int64_t stripped_size)
{
    BlockStructuralFacts facts;
    facts.transaction_count = transactions.size();
    const MerkleRootResult merkle{BlockMerkleRootWithMutation(transactions)};
    facts.merkle_root = merkle.root;
    facts.merkle_mutated = merkle.mutated;
    facts.stripped_size = stripped_size;
    return facts;
}

BlockStructuralFacts ComputeBlockStructuralFacts(const CBlock& block)
{
    return ComputeBlockStructuralFacts(block.vtx, ::GetSerializeSize(TX_NO_WITNESS(block)));
}

BlockFacts ComputeBlockFacts(std::span<const CTransactionRef> transactions, int64_t stripped_size, int64_t weight)
{
    BlockFacts facts;
    facts.structure = ComputeBlockStructuralFacts(transactions, stripped_size);
    facts.witness_merkle_root = BlockWitnessMerkleRoot(transactions);
    facts.has_witness = std::ranges::any_of(transactions, [](const CTransactionRef& tx) {
        return HasWitness(*tx);
    });

    const int witness_commitment_index{Consensus::GetWitnessCommitmentIndex(transactions)};
    if (witness_commitment_index != Consensus::NO_WITNESS_COMMITMENT) {
        facts.witness_commitment_index = witness_commitment_index;
    }

    facts.weight = weight;
    return facts;
}

BlockFacts ComputeBlockFacts(const CBlock& block)
{
    return ComputeBlockFacts(
        block.vtx,
        ::GetSerializeSize(TX_NO_WITNESS(block)),
        Consensus::GetBlockWeight(block));
}

} // namespace Consensus
