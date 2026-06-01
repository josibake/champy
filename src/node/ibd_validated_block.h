// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_IBD_VALIDATED_BLOCK_H
#define BITCOIN_NODE_IBD_VALIDATED_BLOCK_H

#include <node/block_download_types.h>
#include <validation/core_chain_activation.h>

#include <utility>

namespace node {

struct IbdValidatedTipPackage {
    PeerBlockRef block;
    uint256 parent_hash{};
    ExecutedCoreConnectTip execution;

    [[nodiscard]] bool ReadyForSerializedCommit() const noexcept { return true; }
};

[[nodiscard]] inline IbdValidatedTipPackage MakeIbdValidatedTipPackage(ExecutedCoreConnectTip execution)
{
    PeerBlockRef block{
        .hash = execution.block_position.hash,
        .parent_hash = execution.block_position.parent_hash,
        .height = execution.block_position.height,
        .chain_work = execution.block_position.chain_work,
    };
    const uint256 parent_hash{block.parent_hash};
    return {
        .block = std::move(block),
        .parent_hash = parent_hash,
        .execution = std::move(execution),
    };
}

} // namespace node

#endif // BITCOIN_NODE_IBD_VALIDATED_BLOCK_H
