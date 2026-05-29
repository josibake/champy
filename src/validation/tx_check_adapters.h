// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TX_CHECK_ADAPTERS_H
#define BITCOIN_TX_CHECK_ADAPTERS_H

class CTransaction;
class TxValidationState;

bool CheckTransaction(const CTransaction& tx, TxValidationState& state);

#endif // BITCOIN_TX_CHECK_ADAPTERS_H
