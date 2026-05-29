// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <chain.h>
#include <coins.h>
#include <consensus/block_spend.h>
#include <validation/core_block_commit_adapters.h>
#include <kernel/cs_main.h>
#include <primitives/transaction.h>
#include <sync.h>
#include <uint256.h>
#include <undo.h>

namespace {

class FakeBlockUndoWriter final : public BlockUndoWriter
{
public:
    Consensus::BlockCommitResult<void> WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index) override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        wrote_undo = true;
        undo_index = &index;
        written_undo = blockundo;
        return {};
    }

    bool wrote_undo{false};
    CBlockIndex* undo_index{nullptr};
    CBlockUndo written_undo;
};

class FakeBlockIndexCommitter final : public BlockIndexValidityCommitter
{
public:
    void MarkBlockIndexDirty(CBlockIndex& block_index) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { dirty_index = &block_index; }

    CBlockIndex* dirty_index{nullptr};
};

} // namespace

BOOST_AUTO_TEST_SUITE(block_validation_adapters_tests)

BOOST_AUTO_TEST_CASE(core_block_effects_writer_uses_storage_adapters)
{
    LOCK(::cs_main);

    FakeBlockUndoWriter undo_writer;
    FakeBlockIndexCommitter block_index_committer;
    CBlockIndex block_index;
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreBlockEffectsWriter writer{undo_writer, block_index_committer, coins, block_index};

    Consensus::BlockSpendEffects effects;
    effects.transaction_effects.resize(2);
    effects.transaction_effects[1].spends.push_back(Consensus::SpentCoinEffect{
        .outpoint = COutPoint{Txid::FromUint256(uint256::ONE), 0},
        .coin = Consensus::CoinSnapshot{
            .output = CTxOut{50, CScript{}},
            .height = 7,
            .is_coinbase = true,
        },
    });

    BOOST_REQUIRE(writer.WriteBlockRevertData({}, effects));
    BOOST_CHECK(undo_writer.wrote_undo);
    BOOST_CHECK_EQUAL(undo_writer.undo_index, &block_index);
    BOOST_REQUIRE_EQUAL(undo_writer.written_undo.vtxundo.size(), 1U);
    const CTxUndo& transaction_undo{undo_writer.written_undo.vtxundo.front()};
    BOOST_REQUIRE_EQUAL(transaction_undo.vprevout.size(), 1U);
    const Coin& spent_coin{transaction_undo.vprevout.front()};
    BOOST_CHECK(spent_coin.IsCoinBase());
    BOOST_CHECK_EQUAL(spent_coin.nHeight, 7);

    const Consensus::BlockCommitContext context{.new_best_block = uint256::ONE};
    BOOST_REQUIRE(writer.CommitBlockMetadata(context, effects));
    BOOST_CHECK_EQUAL(block_index_committer.dirty_index, &block_index);
    BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK(coins.GetBestBlock() == uint256::ONE);
}

BOOST_AUTO_TEST_SUITE_END()
