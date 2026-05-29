// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <block_data_adapters.h>
#include <block_index_adapters.h>
#include <chain.h>
#include <coins.h>
#include <consensus/block_spend.h>
#include <core_block_commit_adapters.h>
#include <flatfile.h>
#include <kernel/cs_main.h>
#include <primitives/transaction.h>
#include <sync.h>
#include <uint256.h>
#include <undo.h>

#include <optional>
#include <vector>

namespace {

class FakeBlockDataStore final : public BlockDataStore
{
public:
    bool ReadBlock(CBlock&, const CBlockIndex&) override { return false; }
    bool ReadBlockFromPosition(CBlock&, const FlatFilePos&, const std::optional<uint256>&) override { return false; }
    bool ReadBlockUndo(CBlockUndo&, const CBlockIndex&) override { return false; }

    Consensus::BlockCommitResult<void> WriteBlockUndo(const CBlockUndo& blockundo, CBlockIndex& index) override
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        wrote_undo = true;
        undo_index = &index;
        written_undo = blockundo;
        return {};
    }

    bool IsPruneMode() const override { return false; }
    bool HasIndexedBlockFiles() const override { return true; }
    FlatFilePos WriteBlock(const CBlock&, int) override { return {}; }
    void UpdateBlockInfo(const CBlock&, unsigned int, const FlatFilePos&) override {}

    bool wrote_undo{false};
    CBlockIndex* undo_index{nullptr};
    CBlockUndo written_undo;
};

class FakeBlockIndexStore final : public BlockIndexStore
{
public:
    CBlockIndex* LookupBlockIndex(const uint256&) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return nullptr; }
    std::vector<CBlockIndex*> SnapshotBlockIndices() override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return {}; }
    void MarkBlockIndexDirty(CBlockIndex& block_index) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { dirty_index = &block_index; }
    void MarkBlockDataReceived(const CBlock&, CBlockIndex& block_index, const FlatFilePos&) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { received_data_index = &block_index; }
    CBlockIndex* AddToBlockIndex(const CBlockHeader&) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return nullptr; }

    CBlockIndex* dirty_index{nullptr};
    CBlockIndex* received_data_index{nullptr};
};

} // namespace

BOOST_AUTO_TEST_SUITE(block_validation_adapters_tests)

BOOST_AUTO_TEST_CASE(core_block_effects_writer_uses_storage_adapters)
{
    LOCK(::cs_main);

    FakeBlockDataStore block_store;
    FakeBlockIndexStore block_index_store;
    CBlockIndex block_index;
    CCoinsViewCache coins{&CoinsViewEmpty::Get()};
    CoreBlockEffectsWriter writer{block_store, block_index_store, coins, block_index};

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
    BOOST_CHECK(block_store.wrote_undo);
    BOOST_CHECK_EQUAL(block_store.undo_index, &block_index);
    BOOST_REQUIRE_EQUAL(block_store.written_undo.vtxundo.size(), 1U);
    BOOST_REQUIRE_EQUAL(block_store.written_undo.vtxundo[0].vprevout.size(), 1U);
    BOOST_CHECK(block_store.written_undo.vtxundo[0].vprevout[0].IsCoinBase());
    BOOST_CHECK_EQUAL(block_store.written_undo.vtxundo[0].vprevout[0].nHeight, 7);

    const Consensus::BlockCommitContext context{.new_best_block = uint256::ONE};
    BOOST_REQUIRE(writer.CommitBlockMetadata(context, effects));
    BOOST_CHECK_EQUAL(block_index_store.dirty_index, &block_index);
    BOOST_CHECK(block_index.IsValid(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK(coins.GetBestBlock() == uint256::ONE);
}

BOOST_AUTO_TEST_SUITE_END()
