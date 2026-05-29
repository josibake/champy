// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/block_header_context_adapters.h>

#include <chain.h>
#include <chainstate.h>
#include <consensus/amount.h>

Consensus::BlockHeaderContext BuildCoreBlockHeaderContext(const ChainstateManager& chainman, const CBlockIndex* previous_index)
{
    if (previous_index == nullptr) {
        return {};
    }

    return Consensus::BlockHeaderContext{
        previous_index->nHeight + 1,
        previous_index->GetMedianTimePast(),
        previous_index->GetBlockTime(),
        Consensus::BlockDeploymentContext{
            .height_in_coinbase_active = ::DeploymentActiveAfter(previous_index, chainman, Consensus::DEPLOYMENT_HEIGHTINCB),
            .der_signature_active = ::DeploymentActiveAfter(previous_index, chainman, Consensus::DEPLOYMENT_DERSIG),
            .cltv_active = ::DeploymentActiveAfter(previous_index, chainman, Consensus::DEPLOYMENT_CLTV),
            .csv_active = ::DeploymentActiveAfter(previous_index, chainman, Consensus::DEPLOYMENT_CSV),
            .segwit_active = ::DeploymentActiveAfter(previous_index, chainman, Consensus::DEPLOYMENT_SEGWIT),
        },
    };
}

Consensus::BlockConsensusContext BuildCoreBlockConsensusContext(const CBlockIndex& block_index, const ChainstateManager& chainman, const Consensus::Params& consensus_params)
{
    return Consensus::BuildBlockConsensusContext(
        BuildCoreBlockHeaderContext(chainman, block_index.pprev),
        block_index.GetBlockHash(),
        Consensus::CalculateBlockSubsidy(block_index.nHeight, consensus_params));
}
