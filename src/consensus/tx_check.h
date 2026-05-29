// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_TX_CHECK_H
#define BITCOIN_CONSENSUS_TX_CHECK_H

/**
 * Context-independent transaction checking code that can be called outside the
 * bitcoin server and doesn't depend on chain state or local transaction-pool
 * state. Transaction verification code that does call server functions or
 * depend on server state belongs in validation/tx_verify.h/cpp instead.
 */

#include <consensus/expected.h>

#include <string>

class CTransaction;

namespace Consensus {

struct TransactionCheckError {
    std::string reject_reason;
    std::string debug_message;
};

template <typename T>
using TransactionCheckResult = Consensus::Expected<T, TransactionCheckError>;

[[nodiscard]] TransactionCheckResult<void> CheckTransaction(const CTransaction& tx);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_TX_CHECK_H
