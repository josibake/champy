// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_MERKLE_H
#define BITCOIN_CONSENSUS_MERKLE_H

#include <vector>

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <span>

uint256 ComputeMerkleRoot(std::vector<uint256> hashes, bool* mutated = nullptr);

/*
 * Compute the Merkle root of the transactions in a block.
 * *mutated is set to true if a duplicated subtree was found.
 */
uint256 BlockMerkleRoot(std::span<const CTransactionRef> transactions, bool* mutated = nullptr);
uint256 BlockMerkleRoot(const CBlock& block, bool* mutated = nullptr);

/*
 * Compute the Merkle root of the witness transactions in a block.
 */
uint256 BlockWitnessMerkleRoot(std::span<const CTransactionRef> transactions);
uint256 BlockWitnessMerkleRoot(const CBlock& block);

/**
 * Compute merkle path to the specified transaction.
 *
 * @param[in] transactions block transactions
 * @param[in] position transaction for which to calculate the merkle path (0 is the coinbase)
 *
 * @return merkle path ordered from the deepest
 */
std::vector<uint256> TransactionMerklePath(std::span<const CTransactionRef> transactions, uint32_t position);

/**
 * Compute merkle path to the specified transaction.
 *
 * @param[in] block block containing the transaction
 * @param[in] position transaction for which to calculate the merkle path (0 is the coinbase)
 *
 * @return merkle path ordered from the deepest
 */
std::vector<uint256> TransactionMerklePath(const CBlock& block, uint32_t position);

#endif // BITCOIN_CONSENSUS_MERKLE_H
