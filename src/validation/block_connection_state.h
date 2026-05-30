// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_CONNECTION_STATE_H
#define BITCOIN_VALIDATION_BLOCK_CONNECTION_STATE_H

#include <consensus/block_commit.h>
#include <kernel/cs_main.h>
#include <uint256.h>

#include <memory>

namespace validation {

class BlockConnectionAttemptGuard {
public:
    virtual ~BlockConnectionAttemptGuard() = default;

    virtual void Commit() EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class BlockConnectionSpendState {
public:
    virtual ~BlockConnectionSpendState() = default;

    [[nodiscard]] virtual Consensus::BlockSpendWorkspace& Workspace() = 0;
    [[nodiscard]] virtual Consensus::BlockSpendStateCommitter& Committer() = 0;
};

class BlockConnectionState {
public:
    virtual ~BlockConnectionState() = default;

    [[nodiscard]] virtual uint256 BestBlock() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    virtual void SetBestBlock(const uint256& block_hash) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual std::unique_ptr<BlockConnectionAttemptGuard> BeginConnectionAttempt() EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual Consensus::BlockSpendResult<std::unique_ptr<BlockConnectionSpendState>> BeginBlockSpend(
        const Consensus::BlockSpendContext& context,
        std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_BLOCK_CONNECTION_STATE_H
