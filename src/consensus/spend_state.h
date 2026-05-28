// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SPEND_STATE_H
#define BITCOIN_CONSENSUS_SPEND_STATE_H

#include <consensus/amount.h>
#include <primitives/transaction.h>

#include <optional>

class COutPoint;

namespace Consensus {

struct CoinSnapshot {
    CTxOut output;
    int height{0};
    bool is_coinbase{false};
};

class SpendStateView {
public:
    virtual ~SpendStateView() = default;

    [[nodiscard]] virtual bool HaveCoin(const COutPoint& outpoint) const = 0;
    [[nodiscard]] virtual std::optional<CoinSnapshot> GetCoin(const COutPoint& outpoint) const = 0;
};

class SequenceLockTimeView {
public:
    virtual ~SequenceLockTimeView() = default;

    [[nodiscard]] virtual int64_t PreviousMedianTimePast(const COutPoint& outpoint, int coin_height) const = 0;
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SPEND_STATE_H
