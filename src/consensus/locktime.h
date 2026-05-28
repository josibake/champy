// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_LOCKTIME_H
#define BITCOIN_CONSENSUS_LOCKTIME_H

#include <cstdint>

class CTransaction;

/**
 * Check if transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 */
bool IsFinalTx(const CTransaction& tx, int nBlockHeight, int64_t nBlockTime);

#endif // BITCOIN_CONSENSUS_LOCKTIME_H
