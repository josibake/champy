// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_VALIDATION_EVENT_SNAPSHOT_H
#define BITCOIN_VALIDATION_CORE_VALIDATION_EVENT_SNAPSHOT_H

#include <chain.h>
#include <kernel/cs_main.h>
#include <validation/validation_event_queue.h>

namespace validation {

[[nodiscard]] inline ValidationBlockInfo SnapshotCoreValidationBlockInfo(const CBlockIndex& index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return {
        .hash = index.GetBlockHash(),
        .previous_hash = index.pprev ? std::optional<uint256>{index.pprev->GetBlockHash()} : std::nullopt,
        .height = index.nHeight,
        .header = index.GetBlockHeader(),
        .chain_work = index.nChainWork,
        .chain_time_max = index.GetBlockTimeMax(),
        .file_number = index.nFile,
        .data_pos = index.nDataPos,
    };
}

} // namespace validation

#endif // BITCOIN_VALIDATION_CORE_VALIDATION_EVENT_SNAPSHOT_H
