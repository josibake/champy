// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <block_data_admission.h>

BlockDataAdmissionResult GetBlockDataAdmissionResult(const BlockDataAdmissionContext& context)
{
    if (context.already_have_data) {
        return BlockDataAdmissionResult::ALREADY_HAVE_DATA;
    }

    if (context.block_data_requested) {
        return BlockDataAdmissionResult::STORE_BLOCK_DATA;
    }

    if (context.block_data_previously_processed) {
        return BlockDataAdmissionResult::UNREQUESTED_PREVIOUSLY_PROCESSED;
    }

    if (context.active_tip_chain_work && context.block_chain_work < *context.active_tip_chain_work) {
        return BlockDataAdmissionResult::UNREQUESTED_LESS_WORK_THAN_TIP;
    }

    if (context.block_height > context.max_unrequested_height) {
        return BlockDataAdmissionResult::UNREQUESTED_TOO_FAR_AHEAD;
    }

    if (context.block_chain_work < context.minimum_chain_work) {
        return BlockDataAdmissionResult::UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK;
    }

    return BlockDataAdmissionResult::STORE_BLOCK_DATA;
}

bool ShouldStoreBlockData(BlockDataAdmissionResult result)
{
    return result == BlockDataAdmissionResult::STORE_BLOCK_DATA;
}
