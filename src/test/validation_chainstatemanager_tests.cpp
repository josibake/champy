// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
#include <chainparams.h>
#include <consensus/serialization.h>
#include <kernel/block_import_pipeline.h>
#include <kernel/blockimport.h>
#include <validation_state.h>
#include <node/chainstatemanager_args.h>
#include <node/kernel_notifications.h>
#include <random.h>

#include <streams.h>
#include <sync.h>
#include <test/util/common.h>
#include <test/util/logging.h>
#include <test/util/mining.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <test/util/validation.h>
#include <uint256.h>
#include <util/byte_units.h>
#include <util/fs.h>
#include <util/result.h>
#include <util/syserror.h>
#include <util/vector.h>
#include <chainstate.h>
#include <validation/runtime_time.h>
#include <validationinterface.h>

#include <tinyformat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {

constexpr NodeSeconds IMPORT_TEST_TIME{std::chrono::seconds{1'710'000'000}};

void WriteToAutoFile(void* user_data, std::span<const std::byte> bytes)
{
    static_cast<AutoFile*>(user_data)->write(bytes);
}

void WriteBlkRecord(AutoFile& file, const CChainParams& params, const CBlock& block)
{
    const auto block_size{static_cast<uint32_t>(Consensus::SerializedSize(block))};
    file << params.MessageStart() << block_size;
    Consensus::SerializeBlock(block, Consensus::ByteSinkRef{&file, WriteToAutoFile});
}

void WriteMalformedGap(AutoFile& file)
{
    static constexpr std::array<std::byte, 9> NOISE{
        std::byte{0x51}, std::byte{0x00}, std::byte{0xff}, std::byte{0x7e}, std::byte{0x19},
        std::byte{0x33}, std::byte{0x08}, std::byte{0x44}, std::byte{0x91}};
    file.write(NOISE);
}

void WriteTruncatedRecordTail(AutoFile& file, const CChainParams& params)
{
    file << params.MessageStart() << uint32_t{80};
    static constexpr std::array<std::byte, 17> PARTIAL_HEADER{
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x42},
        std::byte{0x24}, std::byte{0x18}, std::byte{0x99}, std::byte{0xab}, std::byte{0xcd},
        std::byte{0xef}, std::byte{0x01}, std::byte{0x55}, std::byte{0x66}, std::byte{0x77},
        std::byte{0x88}, std::byte{0x99}};
    file.write(PARTIAL_HEADER);
}

void CloseWrittenFile(AutoFile& file)
{
    if (file.fclose() != 0) {
        throw std::runtime_error{strprintf("failed to close import test file: %s", SysErrorString(errno))};
    }
}

void WriteExternalBlkFile(const fs::path& path, const CChainParams& params, std::span<const std::shared_ptr<CBlock>> blocks)
{
    AutoFile file{fsbridge::fopen(path, "wb+")};
    if (file.IsNull()) throw std::runtime_error{strprintf("failed to open %s", fs::PathToString(path))};
    for (const auto& block : blocks) {
        WriteBlkRecord(file, params, *block);
    }
    CloseWrittenFile(file);
}

kernel::BlockImportResult ImportExternalBlkFile(ChainstateManager& chainman, const fs::path& path)
{
    chainman.m_blockman.m_blockfiles_indexed = true;
    const std::array<fs::path, 1> import_files{path};
    return kernel::ImportBlocks(chainman, import_files, IMPORT_TEST_TIME);
}

int ActiveHeight(ChainstateManager& chainman)
{
    return WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight());
}

bool HasBlockIndex(ChainstateManager& chainman, const uint256& hash)
{
    return WITH_LOCK(chainman.GetMutex(), return chainman.m_blockman.LookupBlockIndex(hash) != nullptr);
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(validation_chainstatemanager_tests, TestingSetup)

BOOST_FIXTURE_TEST_CASE(block_import_rejects_concurrent_import, ChainTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    chainman.m_blockman.m_importing.store(true, std::memory_order_relaxed);

    const kernel::BlockImportResult result{
        kernel::ImportBlocks(chainman, {}, NodeSeconds{std::chrono::seconds{1'710'000'000}})};

    BOOST_REQUIRE(result);
    BOOST_CHECK(result->status == kernel::BlockImportStatus::AlreadyImporting);
    BOOST_CHECK(chainman.m_blockman.m_importing.load(std::memory_order_relaxed));

    chainman.m_blockman.m_importing.store(false, std::memory_order_relaxed);
}

BOOST_FIXTURE_TEST_CASE(block_import_releases_guard_after_failure, ChainTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    chainman.m_blockman.m_blockfiles_indexed = true;
    chainman.m_blockman.m_importing.store(false, std::memory_order_relaxed);

    const fs::path missing_path{m_args.GetDataDirBase() / "missing-blk.dat"};
    const kernel::BlockImportResult result{
        kernel::ImportBlocks(chainman, {&missing_path, 1}, NodeSeconds{std::chrono::seconds{1'710'000'000}})};

    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().kind == kernel::BlockImportErrorKind::IO);
    BOOST_CHECK(!chainman.m_blockman.m_importing.load(std::memory_order_relaxed));
}

BOOST_FIXTURE_TEST_CASE(block_import_loadblock_accepts_ordered_blocks_across_duplicate_and_malformed_gap, RegTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const auto blocks{CreateBlockChain(/*total_height=*/2, Params())};
    const fs::path import_path{m_path_root / "ordered-import-with-noise.dat"};

    {
        AutoFile file{fsbridge::fopen(import_path, "wb+")};
        BOOST_REQUIRE(!file.IsNull());
        WriteBlkRecord(file, Params(), *blocks[0]);
        WriteBlkRecord(file, Params(), *blocks[0]);
        WriteMalformedGap(file);
        WriteBlkRecord(file, Params(), *blocks[1]);
        CloseWrittenFile(file);
    }

    const kernel::BlockImportResult result{ImportExternalBlkFile(chainman, import_path)};

    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(ActiveHeight(chainman), 2);
    BOOST_CHECK(HasBlockIndex(chainman, blocks[0]->GetHash()));
    BOOST_CHECK(HasBlockIndex(chainman, blocks[1]->GetHash()));
}

BOOST_FIXTURE_TEST_CASE(block_import_loadblock_keeps_prior_block_when_truncated_record_ends_file, RegTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};
    const fs::path import_path{m_path_root / "truncated-tail-import.dat"};

    {
        AutoFile file{fsbridge::fopen(import_path, "wb+")};
        BOOST_REQUIRE(!file.IsNull());
        WriteBlkRecord(file, Params(), *block);
        WriteTruncatedRecordTail(file, Params());
        CloseWrittenFile(file);
    }

    const kernel::BlockImportResult result{ImportExternalBlkFile(chainman, import_path)};

    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(ActiveHeight(chainman), 1);
    BOOST_CHECK(HasBlockIndex(chainman, block->GetHash()));
}

BOOST_FIXTURE_TEST_CASE(block_import_loadblock_skips_unknown_parent_without_failing, RegTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const auto blocks{CreateBlockChain(/*total_height=*/2, Params())};
    const fs::path import_path{m_path_root / "unknown-parent-import.dat"};
    const std::array<std::shared_ptr<CBlock>, 1> child_only{blocks[1]};

    WriteExternalBlkFile(import_path, Params(), child_only);

    const kernel::BlockImportResult result{ImportExternalBlkFile(chainman, import_path)};

    BOOST_REQUIRE(result);
    BOOST_CHECK(result->status == kernel::BlockImportStatus::Completed);
    BOOST_CHECK_EQUAL(ActiveHeight(chainman), 0);
    BOOST_CHECK(!HasBlockIndex(chainman, blocks[1]->GetHash()));
}

BOOST_FIXTURE_TEST_CASE(import_external_block_file_reindex_replays_child_before_parent, RegTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const auto blocks{CreateBlockChain(/*total_height=*/2, Params())};
    const FlatFilePos start_pos{0, 0};

    {
        AutoFile file{chainman.m_blockman.OpenBlockFile(start_pos, /*fReadOnly=*/false)};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE(file.Truncate(0));
        WriteBlkRecord(file, Params(), *blocks[1]);
        WriteBlkRecord(file, Params(), *blocks[0]);
        CloseWrittenFile(file);
    }

    FlatFilePos scan_pos{start_pos};
    kernel::UnknownParentIndex blocks_with_unknown_parent;
    {
        AutoFile file{chainman.m_blockman.OpenBlockFile(scan_pos, /*fReadOnly=*/true)};
        BOOST_REQUIRE(!file.IsNull());
        const auto result{kernel::ImportExternalBlockFile({
            .chainman = chainman,
            .file = file,
            .mode = kernel::ExternalBlockFileReindex{
                .file_number = scan_pos.nFile,
                .unknown_parent_index = blocks_with_unknown_parent,
            },
            .current_time = IMPORT_TEST_TIME,
        })};
        BOOST_REQUIRE(result);
        BOOST_CHECK(result->status == kernel::BlockImportStatus::Completed);
    }

    BOOST_CHECK(blocks_with_unknown_parent.Empty());
    BOOST_REQUIRE(chainman.ActivateBestChains(IMPORT_TEST_TIME));
    BOOST_CHECK_EQUAL(ActiveHeight(chainman), 2);
    BOOST_CHECK(HasBlockIndex(chainman, blocks[0]->GetHash()));
    BOOST_CHECK(HasBlockIndex(chainman, blocks[1]->GetHash()));
}

BOOST_FIXTURE_TEST_CASE(import_external_block_file_reindex_enforces_unknown_parent_limit, RegTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const auto blocks{CreateBlockChain(/*total_height=*/2, Params())};
    const FlatFilePos start_pos{0, 0};

    {
        AutoFile file{chainman.m_blockman.OpenBlockFile(start_pos, /*fReadOnly=*/false)};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE(file.Truncate(0));
        WriteBlkRecord(file, Params(), *blocks[1]);
        CloseWrittenFile(file);
    }

    FlatFilePos scan_pos{start_pos};
    kernel::UnknownParentIndex blocks_with_unknown_parent{/*max_entries=*/0};
    {
        AutoFile file{chainman.m_blockman.OpenBlockFile(scan_pos, /*fReadOnly=*/true)};
        BOOST_REQUIRE(!file.IsNull());
        const auto result{kernel::ImportExternalBlockFile({
            .chainman = chainman,
            .file = file,
            .mode = kernel::ExternalBlockFileReindex{
                .file_number = scan_pos.nFile,
                .unknown_parent_index = blocks_with_unknown_parent,
            },
            .current_time = IMPORT_TEST_TIME,
        })};
        BOOST_REQUIRE(result);
        BOOST_CHECK(result->status == kernel::BlockImportStatus::ResourceLimit);
    }

    BOOST_CHECK(blocks_with_unknown_parent.Empty());
    BOOST_CHECK(!HasBlockIndex(chainman, blocks[1]->GetHash()));
}

BOOST_FIXTURE_TEST_CASE(block_import_loadblock_interrupt_stops_before_scanning_file, RegTestingSetup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const auto block{CreateBlockChain(/*total_height=*/1, Params()).front()};
    const fs::path import_path{m_path_root / "interrupted-import.dat"};
    const std::array<std::shared_ptr<CBlock>, 1> blocks{block};
    WriteExternalBlkFile(import_path, Params(), blocks);

    BOOST_REQUIRE(m_interrupt());
    const kernel::BlockImportResult result{ImportExternalBlkFile(chainman, import_path)};

    BOOST_REQUIRE(result);
    BOOST_CHECK(result->status == kernel::BlockImportStatus::Interrupted);
    BOOST_CHECK_EQUAL(ActiveHeight(chainman), 0);
    BOOST_CHECK(!HasBlockIndex(chainman, block->GetHash()));
    BOOST_CHECK(!chainman.m_blockman.m_importing.load(std::memory_order_relaxed));
    BOOST_REQUIRE(m_interrupt.reset());
}

BOOST_FIXTURE_TEST_CASE(chainstatemanager_ibd_exit_after_loading_blocks, ChainTestingSetup)
{
    CBlockIndex tip;
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    auto apply{[&](bool cached_is_ibd, bool loading_blocks, bool tip_exists, bool enough_work, bool tip_recent) {
        LOCK(::cs_main);
        chainman.ResetChainstates();
        chainman.InitializeChainstate();

        const NodeSeconds current_time{std::chrono::seconds{1'710'000'000}};
        const auto recent_time{current_time - chainman.m_options.max_tip_age};

        chainman.m_cached_is_ibd.store(cached_is_ibd, std::memory_order_relaxed);
        chainman.m_blockman.m_importing = loading_blocks;
        if (tip_exists) {
            tip.nChainWork = chainman.MinimumChainWork() - (enough_work ? 0 : 1);
            tip.nTime = (recent_time - (tip_recent ? 0h : 100h)).time_since_epoch().count();
            chainman.ActiveChain().SetTip(tip);
        } else {
            assert(!chainman.ActiveChain().Tip());
        }
        chainman.UpdateIBDStatus(current_time);
    }};

    for (const bool cached_is_ibd : {false, true}) {
        for (const bool loading_blocks : {false, true}) {
            for (const bool tip_exists : {false, true}) {
                for (const bool enough_work : {false, true}) {
                    for (const bool tip_recent : {false, true}) {
                        apply(cached_is_ibd, loading_blocks, tip_exists, enough_work, tip_recent);
                        const bool expected_ibd = cached_is_ibd && (loading_blocks || !tip_exists || !enough_work || !tip_recent);
                        BOOST_CHECK_EQUAL(chainman.IsInitialBlockDownload(), expected_ibd);
                    }
                }
            }
        }
    }
}

BOOST_FIXTURE_TEST_CASE(active_tip_snapshot_tracks_committed_tip, TestChain100Setup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};

    const auto before{chainman.ActiveTipSnapshot()};
    BOOST_REQUIRE(before);

    uint256 locked_hash;
    int locked_height{-1};
    int64_t locked_time{0};
    arith_uint256 locked_chain_work;
    arith_uint256 locked_block_proof;
    {
        LOCK(chainman.GetMutex());
        const CBlockIndex* tip{chainman.ActiveTip()};
        BOOST_REQUIRE(tip);
        locked_hash = tip->GetBlockHash();
        locked_height = tip->nHeight;
        locked_time = tip->GetBlockTime();
        locked_chain_work = tip->nChainWork;
        locked_block_proof = GetBlockProof(*tip);
    }

    BOOST_CHECK(before->hash == locked_hash);
    BOOST_CHECK_EQUAL(before->height, locked_height);
    BOOST_CHECK_EQUAL(before->time, locked_time);
    BOOST_CHECK(before->chain_work == locked_chain_work);
    BOOST_CHECK(before->block_proof == locked_block_proof);

    const CScript coinbase_script{CScript{} << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    CreateAndProcessBlock({}, coinbase_script);

    const auto after{chainman.ActiveTipSnapshot()};
    BOOST_REQUIRE(after);
    BOOST_CHECK_EQUAL(after->height, before->height + 1);
    BOOST_CHECK(after->hash != before->hash);
    BOOST_CHECK(after->chain_work > before->chain_work);
}

BOOST_FIXTURE_TEST_CASE(best_header_snapshot_tracks_best_header, TestChain100Setup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};

    const auto before{chainman.BestHeaderSnapshot()};
    BOOST_REQUIRE(before);

    uint256 locked_hash;
    int locked_height{-1};
    int64_t locked_time{0};
    {
        LOCK(chainman.GetMutex());
        const CBlockIndex* header{chainman.m_best_header};
        BOOST_REQUIRE(header);
        locked_hash = header->GetBlockHash();
        locked_height = header->nHeight;
        locked_time = header->GetBlockTime();
    }

    BOOST_CHECK(before->hash == locked_hash);
    BOOST_CHECK_EQUAL(before->height, locked_height);
    BOOST_CHECK_EQUAL(before->time, locked_time);

    const CScript coinbase_script{CScript{} << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    CreateAndProcessBlock({}, coinbase_script);

    const auto after{chainman.BestHeaderSnapshot()};
    BOOST_REQUIRE(after);
    BOOST_CHECK_EQUAL(after->height, before->height + 1);
    BOOST_CHECK(after->hash != before->hash);
}

BOOST_FIXTURE_TEST_CASE(copied_block_index_queries_match_locked_state, TestChain100Setup)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};

    uint256 block_hash;
    CBlockHeader block_header;
    uint256 ancestor_hash;
    uint256 next_hash;
    uint256 second_next_hash;
    uint256 third_next_hash;
    int block_height{-1};
    int third_next_height{-1};
    int64_t block_time{0};
    int64_t third_next_time{0};
    int64_t block_median_time_past{0};
    double progress{0};
    arith_uint256 block_chain_work;
    bool segwit_active_at_block{false};
    bool segwit_active_after_block{false};
    CBlockLocator locator;
    CBlockLocator previous_locator;
    arith_uint256 active_tip_buffered_work;
    arith_uint256 active_tip_work;
    int active_tip_height{-1};
    uint256 active_tip_hash;
    int previous_height{-1};
    int best_header_height{-1};
    int64_t best_header_time{0};
    int initial_headers_locator_height{-1};
    CBlockLocator best_header_locator;
    CBlockLocator initial_headers_locator;
    {
        LOCK(chainman.GetMutex());
        const CBlockIndex* block{chainman.ActiveChain()[50]};
        BOOST_REQUIRE(block);
        const CBlockIndex* tip{chainman.ActiveTip()};
        BOOST_REQUIRE(tip);
        const CBlockIndex* best_header{chainman.m_best_header};
        BOOST_REQUIRE(best_header);
        block_hash = block->GetBlockHash();
        block_header = block->GetBlockHeader();
        block_height = block->nHeight;
        block_time = block->GetBlockTime();
        block_median_time_past = block->GetMedianTimePast();
        progress = chainman.GuessVerificationProgress(block, CurrentNodeTime());
        block_chain_work = block->nChainWork;
        segwit_active_at_block = DeploymentActiveAt(*block, chainman, Consensus::DEPLOYMENT_SEGWIT);
        segwit_active_after_block = DeploymentActiveAfter(block, chainman, Consensus::DEPLOYMENT_SEGWIT);
        locator = GetLocator(block);
        const CBlockIndex* previous{Assert(block->pprev)};
        ancestor_hash = previous->GetBlockHash();
        previous_height = previous->nHeight;
        previous_locator = GetLocator(previous);
        next_hash = Assert(chainman.ActiveChain()[51])->GetBlockHash();
        second_next_hash = Assert(chainman.ActiveChain()[52])->GetBlockHash();
        const CBlockIndex* third_next{Assert(chainman.ActiveChain()[53])};
        third_next_hash = third_next->GetBlockHash();
        third_next_height = third_next->nHeight;
        third_next_time = third_next->GetBlockTime();
        active_tip_work = tip->nChainWork;
        active_tip_height = tip->nHeight;
        active_tip_hash = tip->GetBlockHash();
        active_tip_buffered_work = tip->nChainWork -
            std::min<arith_uint256>(144 * GetBlockProof(*tip), tip->nChainWork);
        best_header_height = best_header->nHeight;
        best_header_time = best_header->GetBlockTime();
        best_header_locator = GetLocator(best_header);
        const CBlockIndex* initial_headers_start{best_header->pprev ? best_header->pprev : best_header};
        initial_headers_locator_height = initial_headers_start->nHeight;
        initial_headers_locator = GetLocator(initial_headers_start);
    }

    const auto block{chainman.ActiveChainBlockSnapshot(50)};
    BOOST_REQUIRE(block);
    BOOST_CHECK(block->hash == block_hash);
    BOOST_CHECK_EQUAL(block->height, block_height);
    BOOST_CHECK_EQUAL(block->time, block_time);
    const auto active_tip_chain_work{chainman.ActiveTipChainWorkBlockSnapshot()};
    BOOST_REQUIRE(active_tip_chain_work);
    BOOST_CHECK(active_tip_chain_work->hash == active_tip_hash);
    BOOST_CHECK_EQUAL(active_tip_chain_work->height, active_tip_height);
    BOOST_CHECK(active_tip_chain_work->chain_work == active_tip_work);
    const auto copied_active_tip_work{chainman.ActiveTipWork()};
    BOOST_REQUIRE(copied_active_tip_work);
    BOOST_CHECK(*copied_active_tip_work == active_tip_work);
    const auto copied_active_tip_height{chainman.ActiveTipHeight()};
    BOOST_REQUIRE(copied_active_tip_height);
    BOOST_CHECK_EQUAL(*copied_active_tip_height, active_tip_height);
    const auto copied_active_tip_hash{chainman.ActiveTipHash()};
    BOOST_REQUIRE(copied_active_tip_hash);
    BOOST_CHECK(*copied_active_tip_hash == active_tip_hash);
    const auto active_tip_work_with_buffer{chainman.ActiveTipWorkWithBlockProofBuffer(144)};
    BOOST_REQUIRE(active_tip_work_with_buffer);
    BOOST_CHECK(*active_tip_work_with_buffer == active_tip_buffered_work);
    const auto previous_locator_snapshot{chainman.PreviousBlockLocatorSnapshot(block_hash)};
    BOOST_REQUIRE(previous_locator_snapshot);
    BOOST_CHECK_EQUAL(previous_locator_snapshot->height, previous_height);
    BOOST_CHECK(previous_locator_snapshot->locator.vHave == previous_locator.vHave);
    BOOST_CHECK(!chainman.PreviousBlockLocatorSnapshot(uint256::ONE));
    const auto block_locator_snapshot{chainman.BlockLocatorSnapshot(block_hash)};
    BOOST_REQUIRE(block_locator_snapshot);
    BOOST_CHECK_EQUAL(block_locator_snapshot->height, block_height);
    BOOST_CHECK(block_locator_snapshot->locator.vHave == locator.vHave);
    BOOST_CHECK(!chainman.BlockLocatorSnapshot(uint256::ONE));

    BOOST_CHECK(!chainman.ActiveChainBlockSnapshot(10'000));
    BOOST_CHECK(chainman.HaveActiveChainBlockData(50));
    BOOST_CHECK(chainman.HaveBlocksOnDisk(block_hash, 1, std::nullopt));
    BOOST_CHECK_EQUAL(chainman.GuessVerificationProgress(block_hash), progress);
    const auto chain_work{chainman.BlockChainWork(block_hash)};
    BOOST_REQUIRE(chain_work);
    BOOST_CHECK(*chain_work == block_chain_work);
    BOOST_CHECK(!chainman.BlockChainWork(uint256::ONE));
    const auto chain_work_block{chainman.FindChainWorkBlockSnapshot(block_hash)};
    BOOST_REQUIRE(chain_work_block);
    BOOST_CHECK(chain_work_block->hash == block_hash);
    BOOST_CHECK_EQUAL(chain_work_block->height, block_height);
    BOOST_CHECK(chain_work_block->chain_work == block_chain_work);
    BOOST_CHECK(!chainman.FindChainWorkBlockSnapshot(uint256::ONE));
    const auto known_block{chainman.FindKnownBlockContext(block_hash)};
    BOOST_REQUIRE(known_block);
    BOOST_CHECK(known_block->chain_work == block_chain_work);
    BOOST_CHECK_EQUAL(known_block->segwit_active_after, segwit_active_after_block);
    BOOST_CHECK(!chainman.FindKnownBlockContext(uint256::ONE));
    const auto headers_sync_start{chainman.FindHeadersSyncStart(block_hash)};
    BOOST_REQUIRE(headers_sync_start);
    BOOST_CHECK(headers_sync_start->header.GetHash() == block_header.GetHash());
    BOOST_CHECK_EQUAL(headers_sync_start->height, block_height);
    BOOST_CHECK(headers_sync_start->chain_work == block_chain_work);
    BOOST_CHECK_EQUAL(headers_sync_start->median_time_past, block_median_time_past);
    BOOST_CHECK(headers_sync_start->locator.vHave == locator.vHave);
    BOOST_CHECK(!chainman.FindHeadersSyncStart(uint256::ONE));
    BOOST_CHECK_EQUAL(chainman.FindLocatorForkHeight(locator), block_height);
    BOOST_CHECK(!chainman.HavePruned());
    BOOST_CHECK(!chainman.PruneHeight());
    const auto relay_facts{chainman.FindBlockRelayFacts(block_hash, /*stale_relay_age_seconds=*/30 * 24 * 60 * 60)};
    BOOST_REQUIRE(relay_facts);
    BOOST_CHECK_EQUAL(relay_facts->height, block_height);
    BOOST_CHECK_EQUAL(relay_facts->block_time, block_time);
    BOOST_CHECK(relay_facts->request_allowed);
    BOOST_CHECK(relay_facts->has_data);
    BOOST_CHECK(!relay_facts->needs_active_chain);
    BOOST_REQUIRE(relay_facts->active_tip_height);
    BOOST_CHECK_EQUAL(*relay_facts->active_tip_height, active_tip_height);
    BOOST_REQUIRE(relay_facts->active_tip_hash);
    BOOST_CHECK(*relay_facts->active_tip_hash == active_tip_hash);
    BOOST_CHECK(!chainman.IsBlockPruned(block_hash));
    BOOST_CHECK(!chainman.FindBlockRelayFacts(uint256::ONE, /*stale_relay_age_seconds=*/30 * 24 * 60 * 60));
    const auto inventory{chainman.FindActiveChainInventory(locator, uint256{}, /*max_hashes=*/3, /*pruned_block_depth=*/100)};
    BOOST_CHECK_EQUAL(inventory.first_height, block_height + 1);
    BOOST_REQUIRE_EQUAL(inventory.block_hashes.size(), 3U);
    BOOST_CHECK(inventory.block_hashes[0] == next_hash);
    BOOST_CHECK(inventory.block_hashes[1] == second_next_hash);
    BOOST_CHECK(inventory.block_hashes[2] == third_next_hash);
    BOOST_CHECK(inventory.continuation == third_next_hash);
    BOOST_CHECK(inventory.stop_reason == ActiveChainInventoryStopReason::LIMIT);
    const auto stopped_inventory{chainman.FindActiveChainInventory(locator, second_next_hash, /*max_hashes=*/3, /*pruned_block_depth=*/100)};
    BOOST_REQUIRE_EQUAL(stopped_inventory.block_hashes.size(), 1U);
    BOOST_CHECK(stopped_inventory.block_hashes[0] == next_hash);
    BOOST_CHECK_EQUAL(stopped_inventory.stop_height, block_height + 2);
    BOOST_CHECK(stopped_inventory.stop_hash == second_next_hash);
    BOOST_CHECK(stopped_inventory.stop_reason == ActiveChainInventoryStopReason::STOP_HASH);
    const auto headers{chainman.FindActiveChainHeaders(locator, uint256{}, /*max_headers=*/3, /*stale_relay_age_seconds=*/30 * 24 * 60 * 60)};
    BOOST_CHECK(headers.found);
    BOOST_CHECK(headers.request_allowed);
    BOOST_CHECK_EQUAL(headers.first_height, block_height + 1);
    BOOST_REQUIRE_EQUAL(headers.headers.size(), 3U);
    BOOST_CHECK(headers.headers[0].GetHash() == next_hash);
    BOOST_CHECK(headers.headers[1].GetHash() == second_next_hash);
    BOOST_CHECK(headers.headers[2].GetHash() == third_next_hash);
    BOOST_REQUIRE(headers.best_header_sent);
    BOOST_CHECK(headers.best_header_sent->hash == third_next_hash);
    const auto stopped_headers{chainman.FindActiveChainHeaders(locator, second_next_hash, /*max_headers=*/3, /*stale_relay_age_seconds=*/30 * 24 * 60 * 60)};
    BOOST_REQUIRE_EQUAL(stopped_headers.headers.size(), 2U);
    BOOST_CHECK(stopped_headers.headers[0].GetHash() == next_hash);
    BOOST_CHECK(stopped_headers.headers[1].GetHash() == second_next_hash);
    BOOST_REQUIRE(stopped_headers.best_header_sent);
    BOOST_CHECK(stopped_headers.best_header_sent->hash == second_next_hash);
    {
        std::vector<uint256> announcement_hashes{next_hash, second_next_hash};
        const HeaderAnnouncementResponse announcement{chainman.FindHeaderAnnouncement({
            .block_hashes = std::span<const uint256>{announcement_hashes},
            .peer_best_known_block = block_hash,
        })};
        BOOST_CHECK(!announcement.revert_to_inv);
        BOOST_REQUIRE_EQUAL(announcement.headers.size(), 2U);
        BOOST_CHECK(announcement.headers[0].GetHash() == next_hash);
        BOOST_CHECK(announcement.headers[1].GetHash() == second_next_hash);
        BOOST_REQUIRE(announcement.best_header_sent);
        BOOST_CHECK(announcement.best_header_sent->hash == second_next_hash);
        BOOST_CHECK(announcement.fallback.found);
        BOOST_CHECK(announcement.fallback.in_active_chain);
        BOOST_CHECK(!announcement.fallback.peer_has_header);
    }
    const auto tip_announcement{chainman.FindBlockTipAnnouncementFacts(
        third_next_hash,
        std::optional<uint256>{block_hash},
        /*max_announcements=*/10)};
    BOOST_REQUIRE(tip_announcement);
    BOOST_CHECK_EQUAL(tip_announcement->height, third_next_height);
    BOOST_CHECK_EQUAL(tip_announcement->block_time, third_next_time);
    BOOST_REQUIRE_EQUAL(tip_announcement->block_hashes.size(), 3U);
    BOOST_CHECK(tip_announcement->block_hashes[0] == next_hash);
    BOOST_CHECK(tip_announcement->block_hashes[1] == second_next_hash);
    BOOST_CHECK(tip_announcement->block_hashes[2] == third_next_hash);
    const auto capped_tip_announcement{chainman.FindBlockTipAnnouncementFacts(
        third_next_hash,
        std::optional<uint256>{block_hash},
        /*max_announcements=*/2)};
    BOOST_REQUIRE(capped_tip_announcement);
    BOOST_REQUIRE_EQUAL(capped_tip_announcement->block_hashes.size(), 2U);
    BOOST_CHECK(capped_tip_announcement->block_hashes[0] == second_next_hash);
    BOOST_CHECK(capped_tip_announcement->block_hashes[1] == third_next_hash);
    const auto known_ancestor{chainman.KnownBlockIsAncestorOfBestHeaderOrTip(block_hash)};
    BOOST_REQUIRE(known_ancestor);
    BOOST_CHECK(*known_ancestor);
    BOOST_CHECK(!chainman.KnownBlockIsAncestorOfBestHeaderOrTip(uint256::ONE));
    BOOST_CHECK(chainman.HasBlockIndex(block_hash));
    BOOST_CHECK(!chainman.HasBlockIndex(uint256::ONE));
    {
        LOCK(chainman.GetMutex());
        const auto initial_headers_sync{chainman.InitialHeadersSyncSnapshotLocked()};
        BOOST_REQUIRE(initial_headers_sync);
        BOOST_CHECK_EQUAL(initial_headers_sync->locator_height, initial_headers_locator_height);
        BOOST_CHECK_EQUAL(initial_headers_sync->best_header_time, best_header_time);
        BOOST_CHECK(initial_headers_sync->locator.vHave == initial_headers_locator.vHave);
        const auto pow_valid_announcement{chainman.FindPoWValidBlockAnnouncementFactsLocked(block_hash)};
        BOOST_REQUIRE(pow_valid_announcement);
        BOOST_CHECK(pow_valid_announcement->block.hash == block_hash);
        BOOST_CHECK_EQUAL(pow_valid_announcement->block.height, block_height);
        BOOST_CHECK(pow_valid_announcement->block.chain_work == block_chain_work);
        BOOST_REQUIRE(pow_valid_announcement->previous_hash);
        BOOST_CHECK(*pow_valid_announcement->previous_hash == ancestor_hash);
        BOOST_CHECK_EQUAL(pow_valid_announcement->segwit_active, segwit_active_at_block);
        const auto download_candidates{chainman.FindBlockDownloadCandidatesLocked(
            active_tip_hash,
            block_hash,
            /*download_window=*/2)};
        BOOST_CHECK(download_candidates.found);
        BOOST_CHECK(download_candidates.interesting);
        BOOST_CHECK_EQUAL(download_candidates.best_known_height, active_tip_height);
        BOOST_REQUIRE(download_candidates.last_common);
        BOOST_CHECK(download_candidates.last_common->hash == active_tip_hash);
        BOOST_CHECK(download_candidates.candidates.empty());
        BOOST_CHECK(chainman.ActiveChainContains(*Assert(chainman.ActiveChain()[block_height])));
    }
    const auto best_header{chainman.BestHeaderLocatorSnapshot()};
    BOOST_REQUIRE(best_header);
    BOOST_CHECK_EQUAL(best_header->height, best_header_height);
    BOOST_CHECK(best_header->locator.vHave == best_header_locator.vHave);

    ChainBlockQuery next_query;
    next_query.hash = true;
    next_query.height = true;

    ChainBlockQuery full_query;
    full_query.hash = true;
    full_query.height = true;
    full_query.time = true;
    full_query.max_time = true;
    full_query.median_time_past = true;
    full_query.in_active_chain = true;
    full_query.locator = true;
    full_query.data = true;
    full_query.next_block = std::make_unique<ChainBlockQuery>(std::move(next_query));

    auto found_block{chainman.FindBlock(block_hash, full_query)};
    BOOST_REQUIRE(found_block.found);
    BOOST_CHECK(*found_block.hash == block_hash);
    BOOST_CHECK_EQUAL(*found_block.height, block_height);
    BOOST_CHECK_EQUAL(*found_block.time, block_time);
    BOOST_CHECK(*found_block.in_active_chain);
    BOOST_REQUIRE(found_block.locator);
    BOOST_CHECK_EQUAL(chainman.FindLocatorForkHeight(*found_block.locator), block_height);
    BOOST_REQUIRE(found_block.data);
    BOOST_CHECK(!found_block.data->IsNull());
    BOOST_REQUIRE(found_block.next_block);
    BOOST_REQUIRE(found_block.next_block->found);
    BOOST_CHECK(*found_block.next_block->hash == next_hash);
    BOOST_CHECK_EQUAL(*found_block.next_block->height, block_height + 1);

    ChainBlockQuery height_query;
    height_query.height = true;

    const auto first_by_time{chainman.FindFirstBlockWithTimeAndHeight(block_time, block_height, height_query)};
    BOOST_REQUIRE(first_by_time.found);
    BOOST_CHECK_EQUAL(*first_by_time.height, block_height);

    const auto ancestor_by_height{chainman.FindAncestorByHeight(block_hash, block_height - 1, height_query)};
    BOOST_REQUIRE(ancestor_by_height.found);
    BOOST_CHECK_EQUAL(*ancestor_by_height.height, block_height - 1);

    const auto ancestor_by_hash{chainman.FindAncestorByHash(block_hash, ancestor_hash, height_query)};
    BOOST_REQUIRE(ancestor_by_hash.found);
    BOOST_CHECK_EQUAL(*ancestor_by_hash.height, block_height - 1);

    const auto common_ancestor{chainman.FindCommonAncestor(next_hash, block_hash, height_query, height_query, height_query)};
    BOOST_REQUIRE(common_ancestor.ancestor.found);
    BOOST_REQUIRE(common_ancestor.block1.found);
    BOOST_REQUIRE(common_ancestor.block2.found);
    BOOST_CHECK_EQUAL(*common_ancestor.ancestor.height, block_height);
    BOOST_CHECK_EQUAL(*common_ancestor.block1.height, block_height + 1);
    BOOST_CHECK_EQUAL(*common_ancestor.block2.height, block_height);
}

BOOST_FIXTURE_TEST_CASE(loadblockindex_invalid_descendants, TestChain100Setup)
{
    LOCK(Assert(m_node.chainman)->GetMutex());
    // consider the chain of blocks grand_parent <- parent <- child
    // intentionally mark:
    //   - grand_parent: BLOCK_FAILED_VALID
    //   - parent: BLOCK_FAILED_CHILD
    //   - child: not invalid
    // Test that when the block index is loaded, all blocks are marked as BLOCK_FAILED_VALID
    auto* child{m_node.chainman->ActiveChain().Tip()};
    auto* parent{child->pprev};
    auto* grand_parent{parent->pprev};
    grand_parent->nStatus = (grand_parent->nStatus | BLOCK_FAILED_VALID);
    parent->nStatus = (parent->nStatus & ~BLOCK_FAILED_VALID) | BLOCK_FAILED_CHILD;
    child->nStatus = (child->nStatus & ~BLOCK_FAILED_VALID);

    // Reload block index to recompute block status validity flags.
    m_node.chainman->LoadBlockIndex();

    // check grand_parent, parent, child is marked as BLOCK_FAILED_VALID after reloading the block index
    BOOST_CHECK(grand_parent->nStatus & BLOCK_FAILED_VALID);
    BOOST_CHECK(parent->nStatus & BLOCK_FAILED_VALID);
    BOOST_CHECK(child->nStatus & BLOCK_FAILED_VALID);
}

//! Verify that ReconsiderBlock clears failure flags for the target block, its ancestors, and descendants,
//! but not for sibling forks that diverge from a shared ancestor.
BOOST_FIXTURE_TEST_CASE(invalidate_block_and_reconsider_fork, TestChain100Setup)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    Chainstate& chainstate = chainman.ActiveChainstate();

    // we have a chain of 100 blocks: genesis(0) <- ... <- block98 <- block99 <- block100
    CBlockIndex* block98;
    CBlockIndex* block99;
    CBlockIndex* block100;
    {
        LOCK(chainman.GetMutex());
        block98 = chainman.ActiveChain()[98];
        block99 = chainman.ActiveChain()[99];
        block100 = chainman.ActiveChain()[100];
    }

    // create the following block constellation:
    // genesis(0) <- ... <- block98 <- block99  <- block100
    //                              <- block99' <- block100'
    // by temporarily invalidating block99. the chain tip now falls to block98,
    // mine 2 new blocks on top of block 98 (block99' and block100') and then restore block99 and block 100.
    BlockValidationState state;
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, CurrentNodeTime(), block99));
    BOOST_REQUIRE(WITH_LOCK(cs_main, return chainman.ActiveChain().Tip()) == block98);
    CScript coinbase_script = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    for (int i = 0; i < 2; ++i) {
        CreateAndProcessBlock({}, coinbase_script);
    }
    CBlockIndex* fork_block99;
    CBlockIndex* fork_block100;
    uint256 block98_hash;
    uint256 fork_block99_hash;
    uint256 fork_block100_hash;
    {
        LOCK(chainman.GetMutex());
        fork_block99 = chainman.ActiveChain()[99];
        BOOST_REQUIRE(fork_block99->pprev == block98);
        fork_block100 = chainman.ActiveChain()[100];
        BOOST_REQUIRE(fork_block100->pprev == fork_block99);
        block98_hash = block98->GetBlockHash();
        fork_block99_hash = fork_block99->GetBlockHash();
        fork_block100_hash = fork_block100->GetBlockHash();
    }
    // Restore original block99 and block100
    {
        LOCK(chainman.GetMutex());
        chainstate.ResetBlockFailureFlags(block99);
        chainman.RecalculateBestHeader();
    }
    chainstate.ActivateBestChain(state, CurrentNodeTime());
    BOOST_REQUIRE(WITH_LOCK(cs_main, return chainman.ActiveChain().Tip()) == block100);

    {
        LOCK(chainman.GetMutex());
        BOOST_CHECK(!(block100->nStatus & BLOCK_FAILED_VALID));
        BOOST_CHECK(!(block99->nStatus & BLOCK_FAILED_VALID));
        BOOST_CHECK(!(fork_block100->nStatus & BLOCK_FAILED_VALID));
        BOOST_CHECK(!(fork_block99->nStatus & BLOCK_FAILED_VALID));
        const auto download_candidates{chainman.FindBlockDownloadCandidatesLocked(
            fork_block100_hash,
            block98_hash,
            /*download_window=*/2)};
        BOOST_CHECK(download_candidates.found);
        BOOST_CHECK(download_candidates.interesting);
        BOOST_REQUIRE(download_candidates.last_common);
        BOOST_CHECK(download_candidates.last_common->hash == block98_hash);
        BOOST_REQUIRE_EQUAL(download_candidates.candidates.size(), 2U);
        BOOST_CHECK(download_candidates.candidates[0].block.hash == fork_block99_hash);
        BOOST_CHECK(download_candidates.candidates[0].valid_tree);
        BOOST_CHECK(download_candidates.candidates[0].has_data);
        BOOST_CHECK(!download_candidates.candidates[0].in_active_chain);
        BOOST_CHECK(download_candidates.candidates[1].block.hash == fork_block100_hash);
    }
    {
        LOCK(chainman.GetMutex());
        const uint32_t fork_block99_status{fork_block99->nStatus};
        const uint32_t fork_block100_status{fork_block100->nStatus};
        const unsigned int fork_block100_tx_count{fork_block100->nTx};
        BOOST_CHECK(chainman.BlockValidTransactionsLocked(fork_block100_hash));
        fork_block99->nStatus &= ~BLOCK_HAVE_DATA;
        fork_block100->nStatus &= ~BLOCK_HAVE_DATA;
        fork_block100->nTx = 0;

        const auto direct_fetch{chainman.FindHeadersDirectFetchBlocksLocked({
            .last_header_hash = fork_block100_hash,
            .can_serve_witnesses = true,
            .max_blocks = 2,
        })};
        BOOST_CHECK(direct_fetch.found);
        BOOST_CHECK(direct_fetch.requestable);
        BOOST_CHECK(!direct_fetch.large_reorg);
        BOOST_CHECK(direct_fetch.last_header_parent_valid_chain);
        BOOST_CHECK(direct_fetch.last_header.hash == fork_block100_hash);
        BOOST_CHECK_EQUAL(direct_fetch.blocks.size(), 2U);
        if (direct_fetch.blocks.size() == 2U) {
            BOOST_CHECK(direct_fetch.blocks[0].hash == fork_block99_hash);
            BOOST_CHECK(direct_fetch.blocks[1].hash == fork_block100_hash);
        }

        const auto direct_fetch_over_limit{chainman.FindHeadersDirectFetchBlocksLocked({
            .last_header_hash = fork_block100_hash,
            .can_serve_witnesses = true,
            .max_blocks = 1,
        })};
        BOOST_CHECK(direct_fetch_over_limit.found);
        BOOST_CHECK(direct_fetch_over_limit.requestable);
        BOOST_CHECK(direct_fetch_over_limit.large_reorg);
        BOOST_CHECK(direct_fetch_over_limit.blocks.empty());

        const std::vector<uint256> blocks_in_flight{fork_block100_hash};
        const auto direct_fetch_with_in_flight{chainman.FindHeadersDirectFetchBlocksLocked({
            .last_header_hash = fork_block100_hash,
            .blocks_in_flight = std::span<const uint256>{blocks_in_flight},
            .can_serve_witnesses = true,
            .max_blocks = 2,
        })};
        BOOST_CHECK_EQUAL(direct_fetch_with_in_flight.blocks.size(), 1U);
        if (direct_fetch_with_in_flight.blocks.size() == 1U) {
            BOOST_CHECK(direct_fetch_with_in_flight.blocks[0].hash == fork_block99_hash);
        }

        const auto compact_facts{chainman.FindCompactBlockDownloadFactsLocked(fork_block100_hash)};
        BOOST_CHECK(compact_facts.has_value());
        if (compact_facts) {
            BOOST_CHECK(compact_facts->block.hash == fork_block100_hash);
            BOOST_CHECK(compact_facts->active_tip_available);
            BOOST_CHECK(compact_facts->near_active_tip);
            BOOST_CHECK(!compact_facts->has_block_data);
            BOOST_CHECK(!compact_facts->has_block_transactions);
        }

        fork_block100->nTx = fork_block100_tx_count;
        fork_block99->nStatus = fork_block99_status;
        fork_block100->nStatus = fork_block100_status;
    }

    // Invalidate block98
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, CurrentNodeTime(), block98));

    {
        LOCK(chainman.GetMutex());
        // block98 and all descendants of block98 are marked BLOCK_FAILED_VALID
        BOOST_CHECK(block98->nStatus & BLOCK_FAILED_VALID);
        BOOST_CHECK(block99->nStatus & BLOCK_FAILED_VALID);
        BOOST_CHECK(block100->nStatus & BLOCK_FAILED_VALID);
        BOOST_CHECK(fork_block99->nStatus & BLOCK_FAILED_VALID);
        BOOST_CHECK(fork_block100->nStatus & BLOCK_FAILED_VALID);
    }

    // Reconsider block99. ResetBlockFailureFlags clears BLOCK_FAILED_VALID from
    // block99 and its ancestors (block98) and descendants (block100)
    // but NOT from block99' and block100' (not a direct ancestor/descendant)
    {
        LOCK(chainman.GetMutex());
        chainstate.ResetBlockFailureFlags(block99);
        chainman.RecalculateBestHeader();
    }
    chainstate.ActivateBestChain(state, CurrentNodeTime());
    {
        LOCK(chainman.GetMutex());
        BOOST_CHECK(!(block98->nStatus & BLOCK_FAILED_VALID));
        BOOST_CHECK(!(block99->nStatus & BLOCK_FAILED_VALID));
        BOOST_CHECK(!(block100->nStatus & BLOCK_FAILED_VALID));
        BOOST_CHECK(fork_block99->nStatus & BLOCK_FAILED_VALID);
        BOOST_CHECK(fork_block100->nStatus & BLOCK_FAILED_VALID);
    }
}

/** Helper function to parse args into args_man and return the result of applying them to opts */
template <typename Options>
util::Result<Options> SetOptsFromArgs(ArgsManager& args_man, Options opts,
                                      const std::vector<const char*>& args)
{
    const auto argv{Cat({"ignore"}, args)};
    std::string error{};
    if (!args_man.ParseParameters(argv.size(), argv.data(), error)) {
        return util::Error{Untranslated("ParseParameters failed with error: " + error)};
    }
    const auto result{node::ApplyArgsManOptions(args_man, opts)};
    if (!result) return util::Error{util::ErrorString(result)};
    return opts;
}

BOOST_FIXTURE_TEST_CASE(chainstatemanager_args, BasicTestingSetup)
{
    //! Try to apply the provided args to a ChainstateManager::Options
    auto get_opts = [&](const std::vector<const char*>& args) {
        static kernel::Notifications notifications{};
        static const ChainstateManager::Options options{
            .chainparams = ::Params(),
            .datadir = {},
            .notifications = notifications};
        return SetOptsFromArgs(*this->m_node.args, options, args);
    };
    //! Like get_opts, but requires the provided args to be valid and unwraps the result
    auto get_valid_opts = [&](const std::vector<const char*>& args) {
        const auto result{get_opts(args)};
        BOOST_REQUIRE_MESSAGE(result, util::ErrorString(result).original);
        return *result;
    };

    // test -assumevalid
    BOOST_CHECK(!get_valid_opts({}).assumed_valid_block);
    BOOST_CHECK_EQUAL(get_valid_opts({"-assumevalid="}).assumed_valid_block, uint256::ZERO);
    BOOST_CHECK_EQUAL(get_valid_opts({"-assumevalid=0"}).assumed_valid_block, uint256::ZERO);
    BOOST_CHECK_EQUAL(get_valid_opts({"-noassumevalid"}).assumed_valid_block, uint256::ZERO);
    BOOST_CHECK_EQUAL(get_valid_opts({"-assumevalid=0x12"}).assumed_valid_block, uint256{0x12});

    std::string assume_valid{"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    BOOST_CHECK_EQUAL(get_valid_opts({("-assumevalid=" + assume_valid).c_str()}).assumed_valid_block, uint256::FromHex(assume_valid));

    BOOST_CHECK(!get_opts({"-assumevalid=xyz"}));                                                               // invalid hex characters
    BOOST_CHECK(!get_opts({"-assumevalid=01234567890123456789012345678901234567890123456789012345678901234"})); // > 64 hex chars

    // test -minimumchainwork
    BOOST_CHECK(!get_valid_opts({}).minimum_chain_work);
    BOOST_CHECK_EQUAL(get_valid_opts({"-minimumchainwork=0"}).minimum_chain_work, arith_uint256());
    BOOST_CHECK_EQUAL(get_valid_opts({"-nominimumchainwork"}).minimum_chain_work, arith_uint256());
    BOOST_CHECK_EQUAL(get_valid_opts({"-minimumchainwork=0x1234"}).minimum_chain_work, arith_uint256{0x1234});

    std::string minimum_chainwork{"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    BOOST_CHECK_EQUAL(get_valid_opts({("-minimumchainwork=" + minimum_chainwork).c_str()}).minimum_chain_work, UintToArith256(uint256::FromHex(minimum_chainwork).value()));

    BOOST_CHECK(!get_opts({"-minimumchainwork=xyz"}));                                                               // invalid hex characters
    BOOST_CHECK(!get_opts({"-minimumchainwork=01234567890123456789012345678901234567890123456789012345678901234"})); // > 64 hex chars
}

BOOST_AUTO_TEST_SUITE_END()
