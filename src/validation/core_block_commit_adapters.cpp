// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_block_commit_adapters.h>

#include <chain.h>
#include <coins.h>
#include <undo.h>
#include <validation/block_connection.h>
#include <validation/block_connection_state.h>
#include <validation/block_index.h>
#include <validation/block_storage.h>

#include <cstddef>
#include <new>
#include <stdexcept>
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

bool CoinMatchesSnapshot(const Coin& coin, const Consensus::CoinSnapshot& snapshot)
{
    return coin.out == snapshot.output &&
           static_cast<int>(coin.nHeight) == snapshot.height &&
           coin.IsCoinBase() == snapshot.is_coinbase;
}

Consensus::BlockCommitError StaleSpendStateError(std::string reject_reason = "stale block spend state")
{
    return {
        .runtime_issue = Consensus::ValidationRuntimeIssue::CommitConflict,
        .failure_state = Consensus::BlockCommitFailureState::Unchanged,
        .reject_reason = std::move(reject_reason),
    };
}

Consensus::BlockCommitError TaintedSpendStateError(Consensus::ValidationRuntimeIssue runtime_issue, std::string reject_reason)
{
    return {
        .runtime_issue = runtime_issue,
        .failure_state = Consensus::BlockCommitFailureState::Tainted,
        .reject_reason = std::move(reject_reason),
    };
}

} // namespace

CoreBlockRevertDataWriter::CoreBlockRevertDataWriter(BlockUndoWriter& undo_writer, CBlockIndex& block_index)
    : m_undo_writer{undo_writer}, m_block_index{block_index}
{
}

Consensus::BlockCommitResult<void> CoreBlockRevertDataWriter::WriteBlockRevertData(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects& effects)
{
    return m_undo_writer.WriteBlockUndo(BuildBlockUndoFromSpendEffects(effects), m_block_index);
}

CoreBlockMetadataCommitter::CoreBlockMetadataCommitter(BlockIndexValidityCommitter& block_index_committer, validation::BlockConnectionState& connection_state, CBlockIndex& block_index)
    : m_block_index_committer{block_index_committer}, m_connection_state{connection_state}, m_block_index{block_index}
{
}

Consensus::BlockCommitResult<void> CoreBlockMetadataCommitter::CommitBlockMetadata(const Consensus::BlockCommitContext& context, const Consensus::BlockSpendEffects&)
{
    if (!m_block_index.IsValid(BLOCK_VALID_SCRIPTS)) {
        m_block_index.RaiseValidity(BLOCK_VALID_SCRIPTS);
        m_block_index_committer.MarkBlockIndexDirty(m_block_index);
    }

    m_connection_state.SetBestBlock(context.new_best_block);
    return {};
}

CoreBlockConnectionCommitTarget::CoreBlockConnectionCommitTarget(
    BlockUndoWriter& undo_writer,
    BlockIndexValidityCommitter& block_index_committer,
    validation::BlockConnectionState& connection_state,
    CBlockIndex& block_index)
    : m_block_index{block_index},
      m_revert_data_writer{undo_writer, block_index},
      m_metadata_committer{block_index_committer, connection_state, block_index}
{
}

Consensus::BlockRevertDataWriter& CoreBlockConnectionCommitTarget::RevertDataWriter()
{
    return m_revert_data_writer;
}

Consensus::BlockMetadataCommitter& CoreBlockConnectionCommitTarget::MetadataCommitter()
{
    return m_metadata_committer;
}

validation::BlockConnectionBlockPosition CoreBlockConnectionCommitTarget::BlockPosition() const
{
    return validation::SnapshotBlockConnectionPosition(m_block_index);
}

CoreBlockSpendEffectsCommitter::CoreBlockSpendEffectsCommitter(CCoinsViewCache& view)
    : m_view{view}
{
}

Consensus::BlockCommitResult<void> CoreBlockSpendEffectsCommitter::CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects& effects)
{
    CCoinsViewCache attempt{&m_view};

    for (const Consensus::TransactionCoinEffects& transaction_effects : effects.transaction_effects) {
        for (const Consensus::SpentCoinEffect& spend : transaction_effects.spends) {
            auto current{attempt.GetCoin(spend.outpoint)};
            if (!current || !CoinMatchesSnapshot(*current, spend.coin)) {
                return Consensus::Unexpected<Consensus::BlockCommitError>{StaleSpendStateError()};
            }
            if (!attempt.SpendCoin(spend.outpoint)) {
                return Consensus::Unexpected<Consensus::BlockCommitError>{StaleSpendStateError()};
            }
        }

        for (const Consensus::CreatedCoinEffect& create : transaction_effects.creates) {
            try {
                attempt.AddCoin(create.outpoint, ToCoreCoin(create.coin), /*possible_overwrite=*/create.coin.is_coinbase);
            } catch (const std::logic_error&) {
                return Consensus::Unexpected<Consensus::BlockCommitError>{StaleSpendStateError("stale block create state")};
            }
        }
    }

    try {
        attempt.Flush(/*reallocate_cache=*/false);
    } catch (const std::logic_error&) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedSpendStateError(
            Consensus::ValidationRuntimeIssue::CommitConflict,
            "stale block flush state")};
    } catch (const std::bad_alloc&) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedSpendStateError(
            Consensus::ValidationRuntimeIssue::ResourceLimit,
            "spend-state-flush-resource-limit")};
    } catch (const std::exception&) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedSpendStateError(
            Consensus::ValidationRuntimeIssue::SystemError,
            "spend-state-flush-failed")};
    } catch (...) {
        return Consensus::Unexpected<Consensus::BlockCommitError>{TaintedSpendStateError(
            Consensus::ValidationRuntimeIssue::SystemError,
            "spend-state-flush-failed")};
    }
    return {};
}
