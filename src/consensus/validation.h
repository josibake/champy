// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_VALIDATION_H
#define BITCOIN_CONSENSUS_VALIDATION_H

#include <consensus/consensus.h>
#include <consensus/script_view.h>
#include <primitives/transaction.h>
#include <primitives/block.h>

#include <cstddef>
#include <span>

namespace Consensus {

/** Index marker for when no witness commitment is present in a coinbase transaction. */
static constexpr int NO_WITNESS_COMMITMENT{-1};

/** Minimum size of a witness commitment structure. Defined in BIP 141. **/
static constexpr size_t MINIMUM_WITNESS_COMMITMENT{38};

// These implement the weight = (stripped_size * 4) + witness_size formula,
// using only serialization with and without witness data. As witness_size
// is equal to total_size - stripped_size, this formula is identical to:
// weight = (stripped_size * 3) + total_size.
static inline int32_t GetTransactionWeight(const CTransaction& tx)
{
    return ::GetSerializeSize(TX_NO_WITNESS(tx)) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(TX_WITH_WITNESS(tx));
}
static inline int64_t GetBlockWeight(const CBlock& block)
{
    return ::GetSerializeSize(TX_NO_WITNESS(block)) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(TX_WITH_WITNESS(block));
}
static inline int64_t GetTransactionInputWeight(const CTxIn& txin)
{
    // scriptWitness size is added here because witnesses and txins are split up in segwit serialization.
    return ::GetSerializeSize(TX_NO_WITNESS(txin)) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(TX_WITH_WITNESS(txin)) + ::GetSerializeSize(txin.scriptWitness.stack);
}

/** Compute at which vout of the coinbase transaction the witness commitment occurs, or -1 if not found */
inline int GetWitnessCommitmentIndex(const CTransaction& coinbase)
{
    int commitpos = NO_WITNESS_COMMITMENT;
    for (size_t o = 0; o < coinbase.vout.size(); o++) {
        const CTxOut& vout = coinbase.vout[o];
        if (HasWitnessCommitmentPrefix(ScriptView{vout.scriptPubKey})) {
            commitpos = o;
        }
    }
    return commitpos;
}

/** Compute at which vout of the block's coinbase transaction the witness commitment occurs, or -1 if not found */
inline int GetWitnessCommitmentIndex(std::span<const CTransactionRef> transactions)
{
    if (transactions.empty()) return NO_WITNESS_COMMITMENT;
    return GetWitnessCommitmentIndex(*transactions[0]);
}

inline int GetWitnessCommitmentIndex(const CBlock& block)
{
    return GetWitnessCommitmentIndex(block.vtx);
}

} // namespace Consensus

static constexpr int NO_WITNESS_COMMITMENT{Consensus::NO_WITNESS_COMMITMENT};
static constexpr size_t MINIMUM_WITNESS_COMMITMENT{Consensus::MINIMUM_WITNESS_COMMITMENT};

static inline int32_t GetTransactionWeight(const CTransaction& tx)
{
    return Consensus::GetTransactionWeight(tx);
}
static inline int64_t GetBlockWeight(const CBlock& block)
{
    return Consensus::GetBlockWeight(block);
}
static inline int64_t GetTransactionInputWeight(const CTxIn& txin)
{
    return Consensus::GetTransactionInputWeight(txin);
}
inline int GetWitnessCommitmentIndex(const CBlock& block)
{
    return Consensus::GetWitnessCommitmentIndex(block);
}

#endif // BITCOIN_CONSENSUS_VALIDATION_H
