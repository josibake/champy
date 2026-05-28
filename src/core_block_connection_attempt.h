// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_BLOCK_CONNECTION_ATTEMPT_H
#define BITCOIN_CORE_BLOCK_CONNECTION_ATTEMPT_H

#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_spend.h>
#include <kernel/cs_main.h>

#include <memory>
#include <set>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;

namespace node {
class BlockManager;
} // namespace node

class CoreBlockConnectionAttempt final {
public:
    CoreBlockConnectionAttempt(
        const CBlock& block,
        CBlockIndex& block_index,
        CCoinsViewCache& view,
        node::BlockManager& blockman,
        std::set<CBlockIndex*>& dirty_blockindex,
        Consensus::BlockConsensusContext consensus_context,
        Consensus::BlockSpendConsensusOptions spend_options);
    ~CoreBlockConnectionAttempt();

    CoreBlockConnectionAttempt(const CoreBlockConnectionAttempt&) = delete;
    CoreBlockConnectionAttempt& operator=(const CoreBlockConnectionAttempt&) = delete;
    CoreBlockConnectionAttempt(CoreBlockConnectionAttempt&&) noexcept;
    CoreBlockConnectionAttempt& operator=(CoreBlockConnectionAttempt&&) noexcept;

    [[nodiscard]] Consensus::BlockSpendResult<Consensus::BlockSpendEffects> ValidateAndStageSpend(Consensus::BlockScriptChecker& script_checker)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] Consensus::BlockSpendResult<Consensus::BlockSpendEffects> CompleteSpendStage(
        Consensus::BlockSpendResult<Consensus::BlockSpendEffects> spend_effects,
        Consensus::BlockScriptChecker& script_checker) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] Consensus::BlockCommitResult<void> WriteUndoAndCommitSpendState(const Consensus::BlockSpendEffects& effects)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] Consensus::BlockCommitResult<void> CommitBlockIndex(const Consensus::BlockSpendEffects& effects)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // BITCOIN_CORE_BLOCK_CONNECTION_ATTEMPT_H
