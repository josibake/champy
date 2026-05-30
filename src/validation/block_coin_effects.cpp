// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_coin_effects.h>

#include <coins.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <undo.h>

#include <cassert>
#include <vector>

namespace validation {

namespace {

Coin ToCoreCoin(const Consensus::CoinSnapshot& coin)
{
    return Coin{coin.output, coin.height, coin.is_coinbase};
}

Consensus::CoinSnapshot ToConsensusCoin(const Coin& coin)
{
    return Consensus::CoinSnapshot{
        .output = coin.out,
        .height = static_cast<int>(coin.nHeight),
        .is_coinbase = coin.IsCoinBase(),
    };
}

} // namespace

void ApplyTransactionCoinEffectsForBlock(const Consensus::TransactionCoinEffects& effects, CCoinsViewCache& coins, CTxUndo& undo)
{
    undo.vprevout.reserve(effects.spends.size());
    for (const Consensus::SpentCoinEffect& spend : effects.spends) {
        undo.vprevout.push_back(ToCoreCoin(spend.coin));
        const bool is_spent{coins.SpendCoin(spend.outpoint)};
        assert(is_spent);
    }

    for (const Consensus::CreatedCoinEffect& create : effects.creates) {
        coins.AddCoin(create.outpoint, ToCoreCoin(create.coin), /*possible_overwrite=*/create.coin.is_coinbase);
    }
}

void StageTransactionCoinsForBlock(const CTransaction& tx, CCoinsViewCache& coins, CTxUndo& undo, int block_height)
{
    std::vector<Consensus::CoinSnapshot> input_coins;
    if (!tx.IsCoinBase()) {
        input_coins.reserve(tx.vin.size());
        for (const CTxIn& txin : tx.vin) {
            const auto coin{coins.GetCoin(txin.prevout)};
            assert(coin);
            input_coins.push_back(ToConsensusCoin(*coin));
        }
    }
    const Consensus::TransactionCoinEffects effects{Consensus::BuildTransactionCoinEffectsForBlock(tx, input_coins, block_height)};
    ApplyTransactionCoinEffectsForBlock(effects, coins, undo);
}

void ReplayTransactionCoinsForRecovery(const CTransaction& tx, CCoinsViewCache& coins, int block_height)
{
    if (!tx.IsCoinBase()) {
        for (const CTxIn& txin : tx.vin) {
            coins.SpendCoin(txin.prevout);
        }
    }

    AddCoins(coins, tx, block_height, /*possible_overwrite=*/true);
}

void ReplayBlockCoinsForRecovery(const CBlock& block, CCoinsViewCache& coins, int block_height)
{
    for (const CTransactionRef& tx : block.vtx) {
        ReplayTransactionCoinsForRecovery(*tx, coins, block_height);
    }
}

void CommitStagedCoinsForBlock(CCoinsViewCache& staged_coins, CCoinsViewCache& coins)
{
    staged_coins.SetBestBlock(coins.GetBestBlock());
    staged_coins.Flush(/*reallocate_cache=*/false);
}

} // namespace validation
