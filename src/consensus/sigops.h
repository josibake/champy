// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SIGOPS_H
#define BITCOIN_CONSENSUS_SIGOPS_H

class CTransaction;

namespace Consensus {

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way.
 *
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
unsigned int GetLegacySigOpCount(const CTransaction& tx);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SIGOPS_H
