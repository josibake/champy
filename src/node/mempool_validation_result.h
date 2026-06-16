// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_MEMPOOL_VALIDATION_RESULT_H
#define BITCOIN_NODE_MEMPOOL_VALIDATION_RESULT_H

#include <validation_state.h>

#include <cassert>

/** Result vocabulary for transaction admission to a node's mempool. */
enum class MempoolValidationResult {
    RESULT_UNSET = 0,     //!< initial value. Tx has not yet been rejected
    CONSENSUS,            //!< invalid by consensus rules
    INPUTS_NOT_STANDARD,  //!< inputs (covered by txid) failed policy rules
    NOT_STANDARD,         //!< otherwise didn't meet our local policy rules
    MISSING_INPUTS,       //!< transaction was missing some of its inputs
    PREMATURE_SPEND,      //!< transaction spends a coinbase too early, or violates locktime/sequence locks
    /**
     * Transaction might have a witness prior to SegWit activation, or witness may have been
     * malleated (which includes non-standard witnesses).
     */
    WITNESS_MUTATED,
    /** Transaction is missing a witness. */
    WITNESS_STRIPPED,
    /**
     * Tx already in mempool or conflicts with a tx in the chain. If it conflicts with another tx
     * in mempool, MEMPOOL_POLICY is used when it fails to reach the RBF threshold.
     */
    CONFLICT,
    MEMPOOL_POLICY,       //!< violated mempool's fee/size/descendant/RBF/etc limits
    NO_MEMPOOL,           //!< this node does not have a mempool so can't validate the transaction
    RECONSIDERABLE,       //!< fails some policy, but might be acceptable if submitted in a package
    UNKNOWN,              //!< transaction was not validated because package failed
};

class MempoolValidationState : public ValidationState<MempoolValidationResult> {};

[[nodiscard]] inline MempoolValidationResult ToMempoolValidationResult(TxValidationResult result)
{
    switch (result) {
    case TxValidationResult::TX_RESULT_UNSET:
        return MempoolValidationResult::RESULT_UNSET;
    case TxValidationResult::TX_CONSENSUS:
        return MempoolValidationResult::CONSENSUS;
    case TxValidationResult::TX_INPUTS_NOT_STANDARD:
        return MempoolValidationResult::INPUTS_NOT_STANDARD;
    case TxValidationResult::TX_NOT_STANDARD:
        return MempoolValidationResult::NOT_STANDARD;
    case TxValidationResult::TX_MISSING_INPUTS:
        return MempoolValidationResult::MISSING_INPUTS;
    case TxValidationResult::TX_PREMATURE_SPEND:
        return MempoolValidationResult::PREMATURE_SPEND;
    case TxValidationResult::TX_WITNESS_MUTATED:
        return MempoolValidationResult::WITNESS_MUTATED;
    case TxValidationResult::TX_WITNESS_STRIPPED:
        return MempoolValidationResult::WITNESS_STRIPPED;
    case TxValidationResult::TX_CONFLICT:
        return MempoolValidationResult::CONFLICT;
    case TxValidationResult::TX_MEMPOOL_POLICY:
        return MempoolValidationResult::MEMPOOL_POLICY;
    case TxValidationResult::TX_NO_MEMPOOL:
        return MempoolValidationResult::NO_MEMPOOL;
    case TxValidationResult::TX_RECONSIDERABLE:
        return MempoolValidationResult::RECONSIDERABLE;
    case TxValidationResult::TX_UNKNOWN:
        return MempoolValidationResult::UNKNOWN;
    }

    assert(false);
    return MempoolValidationResult::UNKNOWN;
}

[[nodiscard]] inline MempoolValidationState ToMempoolValidationState(const TxValidationState& tx_state)
{
    MempoolValidationState mempool_state;
    if (tx_state.IsInvalid()) {
        mempool_state.Invalid(ToMempoolValidationResult(tx_state.GetResult()),
                              tx_state.GetRejectReason(),
                              tx_state.GetDebugMessage());
    } else if (tx_state.IsError()) {
        mempool_state.Error(tx_state.GetRejectReason());
    } else {
        assert(tx_state.IsValid());
    }
    return mempool_state;
}

#endif // BITCOIN_NODE_MEMPOOL_VALIDATION_RESULT_H
