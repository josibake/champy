// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_POLICY_H
#define BITCOIN_BLOCK_VALIDATION_POLICY_H

#include <consensus/block_check.h>
#include <script/verify_flags.h>

class CBlockIndex;
class uint256;

namespace Consensus {
struct Params;
} // namespace Consensus

/** Identifies blocks that overwrote an existing coinbase output in the UTXO set (see BIP30) */
bool IsBIP30Repeat(const CBlockIndex& block_index);

/** Identifies blocks which coinbase output was subsequently overwritten in the UTXO set (see BIP30) */
bool IsBIP30Unspendable(const uint256& block_hash, int block_height);

/** Return the script verification flags which should be checked for a given block */
script_verify_flags GetBlockScriptFlags(const CBlockIndex& block_index, const Consensus::Params& consensus_params, Consensus::BlockDeploymentContext deployments);

#endif // BITCOIN_BLOCK_VALIDATION_POLICY_H
