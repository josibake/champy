// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_COIN_EFFECTS_H
#define BITCOIN_CONSENSUS_COIN_EFFECTS_H

#include <consensus/spend_state.h>

#include <span>
#include <vector>

namespace Consensus {

struct SpentCoinEffect {
    COutPoint outpoint;
    CoinSnapshot coin;
};

struct CreatedCoinEffect {
    COutPoint outpoint;
    CoinSnapshot coin;
};

struct TransactionCoinEffects {
    std::vector<SpentCoinEffect> spends;
    std::vector<CreatedCoinEffect> creates;
};

[[nodiscard]] TransactionCoinEffects BuildTransactionCoinEffectsForBlock(const CTransaction& tx, std::span<const CoinSnapshot> input_coins, int block_height);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_COIN_EFFECTS_H
