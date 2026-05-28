// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_COIN_EFFECTS_H
#define BITCOIN_BLOCK_COIN_EFFECTS_H

#include <consensus/coin_effects.h>

class CCoinsViewCache;
class CTransaction;
class CTxUndo;

namespace Consensus {

void ApplyTransactionCoinEffectsForBlock(const TransactionCoinEffects& effects, CCoinsViewCache& coins, CTxUndo& undo);
void StageTransactionCoinsForBlock(const CTransaction& tx, CCoinsViewCache& coins, CTxUndo& undo, int block_height);
void CommitStagedCoinsForBlock(CCoinsViewCache& staged_coins, CCoinsViewCache& coins);

} // namespace Consensus

#endif // BITCOIN_BLOCK_COIN_EFFECTS_H
