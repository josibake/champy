// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_DATA_ADMISSION_H
#define BITCOIN_BLOCK_DATA_ADMISSION_H

#include <arith_uint256.h>

#include <optional>

enum class BlockDataAdmissionResult {
    STORE_BLOCK_DATA,
    ALREADY_HAVE_DATA,
    UNREQUESTED_PREVIOUSLY_PROCESSED,
    UNREQUESTED_LESS_WORK_THAN_TIP,
    UNREQUESTED_TOO_FAR_AHEAD,
    UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK,
};

struct BlockDataAdmissionContext {
    bool already_have_data{false};
    bool block_data_requested{false};
    bool block_data_previously_processed{false};
    int block_height{0};
    int max_unrequested_height{0};
    arith_uint256 block_chain_work{};
    std::optional<arith_uint256> active_tip_chain_work{};
    arith_uint256 minimum_chain_work{};
};

[[nodiscard]] BlockDataAdmissionResult GetBlockDataAdmissionResult(const BlockDataAdmissionContext& context);

[[nodiscard]] bool ShouldStoreBlockData(BlockDataAdmissionResult result);

#endif // BITCOIN_BLOCK_DATA_ADMISSION_H
