// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/coin_effects.h>

#include <consensus/predicates.h>
#include <primitives/transaction.h>

#include <cassert>
#include <cstddef>

namespace Consensus {

TransactionCoinEffects BuildTransactionCoinEffectsForBlock(const CTransaction& tx, std::span<const CoinSnapshot> input_coins, int block_height)
{
    TransactionCoinEffects effects;
    const bool is_coinbase{IsCoinbase(tx)};

    if (!is_coinbase) {
        assert(input_coins.size() == tx.vin.size());
        effects.spends.reserve(tx.vin.size());
        for (std::size_t input_index{0}; input_index < tx.vin.size(); ++input_index) {
            const CTxIn& txin{tx.vin[input_index]};
            effects.spends.push_back({
                .outpoint = txin.prevout,
                .coin = input_coins[input_index],
            });
        }
    } else {
        assert(input_coins.empty());
    }

    const Txid& txid{tx.GetHash()};
    effects.creates.reserve(tx.vout.size());
    for (size_t output_index{0}; output_index < tx.vout.size(); ++output_index) {
        const CTxOut& txout{tx.vout[output_index]};
        if (IsUnspendable(txout)) continue;
        effects.creates.push_back({
            .outpoint = COutPoint{txid, static_cast<uint32_t>(output_index)},
            .coin = CoinSnapshot{
                .output = txout,
                .height = block_height,
                .is_coinbase = is_coinbase,
            },
        });
    }

    return effects;
}

} // namespace Consensus
