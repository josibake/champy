// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <block_validation_policy.h>

#include <chain.h>
#include <chainstate.h>
#include <consensus/params.h>
#include <script/interpreter.h>
#include <uint256.h>
#include <util/check.h>

script_verify_flags GetBlockScriptFlags(const CBlockIndex& block_index, const ChainstateManager& chainman)
{
    const Consensus::Params& consensusparams = chainman.GetConsensus();

    // BIP16 didn't become active until Apr 1 2012 (on mainnet, and
    // retroactively applied to testnet)
    // However, only one historical block violated the P2SH rules (on both
    // mainnet and testnet).
    // Similarly, only one historical block violated the TAPROOT rules on
    // mainnet.
    // For simplicity, always leave P2SH+WITNESS+TAPROOT on except for the two
    // violating blocks.
    script_verify_flags flags{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT};
    const auto it{consensusparams.script_flag_exceptions.find(*Assert(block_index.phashBlock))};
    if (it != consensusparams.script_flag_exceptions.end()) {
        flags = it->second;
    }

    // Enforce the DERSIG (BIP66) rule
    if (DeploymentActiveAt(block_index, chainman, Consensus::DEPLOYMENT_DERSIG)) {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Enforce CHECKLOCKTIMEVERIFY (BIP65)
    if (DeploymentActiveAt(block_index, chainman, Consensus::DEPLOYMENT_CLTV)) {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Enforce CHECKSEQUENCEVERIFY (BIP112)
    if (DeploymentActiveAt(block_index, chainman, Consensus::DEPLOYMENT_CSV)) {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // Enforce BIP147 NULLDUMMY (activated simultaneously with segwit)
    if (DeploymentActiveAt(block_index, chainman, Consensus::DEPLOYMENT_SEGWIT)) {
        flags |= SCRIPT_VERIFY_NULLDUMMY;
    }

    return flags;
}

bool IsBIP30Repeat(const CBlockIndex& block_index)
{
    return (block_index.nHeight == 91842 && block_index.GetBlockHash() == uint256{"00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec"}) ||
           (block_index.nHeight == 91880 && block_index.GetBlockHash() == uint256{"00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721"});
}

bool IsBIP30Unspendable(const uint256& block_hash, int block_height)
{
    return (block_height == 91722 && block_hash == uint256{"00000000000271a2dc26e7667f8419f2e15416dc6955e5a6c6cdf3f2574dd08e"}) ||
           (block_height == 91812 && block_hash == uint256{"00000000000af0aed4792b1acee3d966af36cf5def14935db8de83d6f9306f2f"});
}
