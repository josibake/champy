// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_block_commit_adapters.h>

#include <block_coin_effects.h>
#include <chain.h>
#include <coins.h>
#include <node/blockstorage.h>
#include <undo.h>

#include <cstddef>
#include <utility>

namespace {

Coin ToCoreCoin(const Consensus::CoinSnapshot& coin)
{
    return Coin{coin.output, coin.height, coin.is_coinbase};
}

CBlockUndo BuildBlockUndoFromSpendEffects(const Consensus::BlockSpendEffects& effects)
{
    CBlockUndo block_undo;
    if (effects.transaction_effects.size() <= 1) return block_undo;

    block_undo.vtxundo.reserve(effects.transaction_effects.size() - 1);
    for (std::size_t i{1}; i < effects.transaction_effects.size(); ++i) {
        const Consensus::TransactionCoinEffects& transaction_effects{effects.transaction_effects[i]};
        CTxUndo transaction_undo;
        transaction_undo.vprevout.reserve(transaction_effects.spends.size());
        for (const Consensus::SpentCoinEffect& spend : transaction_effects.spends) {
            transaction_undo.vprevout.push_back(ToCoreCoin(spend.coin));
        }
        block_undo.vtxundo.push_back(std::move(transaction_undo));
    }

    return block_undo;
}

} // namespace

CoreBlockEffectsWriter::CoreBlockEffectsWriter(node::BlockManager& blockman, std::set<CBlockIndex*>& dirty_blockindex, CCoinsViewCache& view, CBlockIndex& block_index)
    : m_blockman{blockman}, m_dirty_blockindex{dirty_blockindex}, m_view{view}, m_block_index{block_index}
{
}

Consensus::BlockCommitResult<void> CoreBlockEffectsWriter::WriteBlockRevertData(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects& effects)
{
    if (const auto undo_write{m_blockman.WriteBlockUndo(BuildBlockUndoFromSpendEffects(effects), m_block_index)}; !undo_write) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{Consensus::BlockCommitError{
            .reject_reason = undo_write.error().reject_reason,
        }};
    }
    return {};
}

Consensus::BlockCommitResult<void> CoreBlockEffectsWriter::CommitBlockMetadata(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects&)
{
    if (!m_block_index.IsValid(BLOCK_VALID_SCRIPTS)) {
        m_block_index.RaiseValidity(BLOCK_VALID_SCRIPTS);
        m_dirty_blockindex.insert(&m_block_index);
    }

    m_view.SetBestBlock(context.new_best_block);
    return {};
}

CoreBlockSpendStateCommitter::CoreBlockSpendStateCommitter(CCoinsViewCache& staged_view, CCoinsViewCache& view)
    : m_staged_view{staged_view}, m_view{view}
{
}

Consensus::BlockCommitResult<void> CoreBlockSpendStateCommitter::CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&)
{
    Consensus::CommitStagedCoinsForBlock(m_staged_view, m_view);
    return {};
}
