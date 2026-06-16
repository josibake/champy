// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/sequence_locks.h>

#include <consensus/consensus.h>
#include <consensus/predicates.h>
#include <primitives/transaction.h>

#include <cassert>
#include <cstddef>

namespace {

bool EnforceBIP68SequenceLocks(const CTransaction& tx, int flags)
{
    return tx.version >= 2 && flags & LOCKTIME_VERIFY_SEQUENCE;
}

} // namespace

std::pair<int, int64_t> Consensus::CalculateSequenceLocks(const CTransaction& tx, int flags, const SequenceLockContext& context)
{
    assert(context.inputs.size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!EnforceBIP68SequenceLocks(tx, flags)) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (!HasRelativeLocktime(txin)) {
            continue;
        }

        const SequenceLockInputContext& input_context{context.inputs[txinIndex]};

        if (RelativeLocktimeIsTime(txin)) {
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, input_context.previous_median_time_past + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, input_context.height + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool Consensus::EvaluateSequenceLocks(const SequenceLockContext& context, std::pair<int, int64_t> lock_pair)
{
    return Consensus::EvaluateSequenceLocksAtBlock(context.block_height, context.previous_median_time_past, lock_pair);
}

bool Consensus::EvaluateSequenceLocksAtBlock(int block_height, int64_t previous_median_time_past, std::pair<int, int64_t> lock_pair)
{
    if (lock_pair.first >= block_height || lock_pair.second >= previous_median_time_past) return false;

    return true;
}

bool Consensus::SequenceLocks(const CTransaction& tx, int flags, const SequenceLockContext& context)
{
    return Consensus::EvaluateSequenceLocks(context, Consensus::CalculateSequenceLocks(tx, flags, context));
}
