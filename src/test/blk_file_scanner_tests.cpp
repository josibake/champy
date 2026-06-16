// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/serialization.h>
#include <kernel/blk_file_scanner.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/fs.h>
#include <util/signalinterrupt.h>
#include <util/syserror.h>
#include <util/vector.h>

#include <tinyformat.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <stdexcept>

#include <boost/test/unit_test.hpp>

namespace {

void WriteToAutoFile(void* user_data, std::span<const std::byte> bytes)
{
    static_cast<AutoFile*>(user_data)->write(bytes);
}

CBlock MakeScanTestBlock(uint32_t nonce)
{
    CBlock block;
    block.nVersion = 1;
    block.nTime = 1'710'000'000;
    block.nBits = 0x207fffff;
    block.nNonce = nonce;
    return block;
}

void WriteBlkRecord(AutoFile& file, const CChainParams& params, const CBlock& block)
{
    const auto block_size{static_cast<uint32_t>(Consensus::SerializedSize(block))};
    file << params.MessageStart() << block_size;
    Consensus::SerializeBlock(block, Consensus::ByteSinkRef{&file, WriteToAutoFile});
}

void CloseWrittenFile(AutoFile& file)
{
    if (file.fclose() != 0) {
        throw std::runtime_error{strprintf("failed to close scanner test file: %s", SysErrorString(errno))};
    }
}

void CheckStatus(const kernel::BlkScanResult& result, kernel::BlkScanStatus expected)
{
    BOOST_CHECK_EQUAL(static_cast<int>(result.status()), static_cast<int>(expected));
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(blk_file_scanner_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(scanner_reports_recoverable_framing_outcomes_before_record)
{
    const fs::path path{m_path_root / "scanner-recoverable.dat"};
    const CBlock block{MakeScanTestBlock(/*nonce=*/7)};
    const auto marker{Params().MessageStart()};

    {
        AutoFile file{fsbridge::fopen(path, "wb+")};
        BOOST_REQUIRE(!file.IsNull());
        const std::array<std::byte, 4> wrong_marker{
            static_cast<std::byte>(marker[0]), std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
        file.write(wrong_marker);
        file << marker << uint32_t{MAX_BLOCK_SERIALIZED_SIZE + 1};
        WriteBlkRecord(file, Params(), block);
        CloseWrittenFile(file);
    }

    AutoFile file{fsbridge::fopen(path, "rb")};
    BOOST_REQUIRE(!file.IsNull());
    kernel::BlkFileScanner scanner{file, marker};

    const kernel::BlkScanResult mismatch{scanner.Next()};
    CheckStatus(mismatch, kernel::BlkScanStatus::MagicMismatch);
    BOOST_CHECK(mismatch.IsRecoverable());

    const kernel::BlkScanResult oversized{scanner.Next()};
    CheckStatus(oversized, kernel::BlkScanStatus::OversizedRecord);
    BOOST_CHECK_EQUAL(oversized.payload_size(), MAX_BLOCK_SERIALIZED_SIZE + 1);
    BOOST_CHECK(oversized.IsRecoverable());

    const kernel::BlkScanResult record{scanner.Next()};
    CheckStatus(record, kernel::BlkScanStatus::Record);
    BOOST_REQUIRE(record.IsRecord());
    BOOST_CHECK_EQUAL(record.record().hash.ToString(), block.GetHash().ToString());

    CheckStatus(scanner.Next(), kernel::BlkScanStatus::Eof);
}

BOOST_AUTO_TEST_CASE(scanner_reports_short_record_tail)
{
    const fs::path path{m_path_root / "scanner-short-record.dat"};
    const auto marker{Params().MessageStart()};

    {
        AutoFile file{fsbridge::fopen(path, "wb+")};
        BOOST_REQUIRE(!file.IsNull());
        file << marker << uint32_t{80};
        static constexpr std::array<std::byte, 9> PARTIAL_HEADER{
            std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x11},
            std::byte{0x22}, std::byte{0x33}, std::byte{0x44}, std::byte{0x55}};
        file.write(PARTIAL_HEADER);
        CloseWrittenFile(file);
    }

    AutoFile file{fsbridge::fopen(path, "rb")};
    BOOST_REQUIRE(!file.IsNull());
    kernel::BlkFileScanner scanner{file, marker};

    const kernel::BlkScanResult short_read{scanner.Next()};
    CheckStatus(short_read, kernel::BlkScanStatus::ShortRead);
    BOOST_CHECK(short_read.IsRecoverable());
}

BOOST_AUTO_TEST_CASE(scanner_reads_exact_record_payload)
{
    const fs::path path{m_path_root / "scanner-read-block.dat"};
    const CBlock block{MakeScanTestBlock(/*nonce=*/11)};

    {
        AutoFile file{fsbridge::fopen(path, "wb+")};
        BOOST_REQUIRE(!file.IsNull());
        WriteBlkRecord(file, Params(), block);
        CloseWrittenFile(file);
    }

    AutoFile file{fsbridge::fopen(path, "rb")};
    BOOST_REQUIRE(!file.IsNull());
    kernel::BlkFileScanner scanner{file, Params().MessageStart()};

    const kernel::BlkScanResult result{scanner.Next()};
    CheckStatus(result, kernel::BlkScanStatus::Record);

    CBlock read_block;
    scanner.ReadBlock(read_block, result.record());
    BOOST_CHECK_EQUAL(read_block.GetHash().ToString(), block.GetHash().ToString());
    CheckStatus(scanner.Next(), kernel::BlkScanStatus::Eof);
}

BOOST_AUTO_TEST_CASE(scanner_reports_interruption_as_scan_status)
{
    const fs::path path{m_path_root / "scanner-interrupted.dat"};
    const CBlock block{MakeScanTestBlock(/*nonce=*/13)};

    {
        AutoFile file{fsbridge::fopen(path, "wb+")};
        BOOST_REQUIRE(!file.IsNull());
        WriteBlkRecord(file, Params(), block);
        CloseWrittenFile(file);
    }

    AutoFile file{fsbridge::fopen(path, "rb")};
    BOOST_REQUIRE(!file.IsNull());
    util::SignalInterrupt interrupt;
    BOOST_REQUIRE(interrupt());
    kernel::BlkFileScanner scanner{file, Params().MessageStart(), &interrupt};

    const kernel::BlkScanResult result{scanner.Next()};
    CheckStatus(result, kernel::BlkScanStatus::Interrupted);
    BOOST_CHECK(result.IsInterrupted());
}

BOOST_AUTO_TEST_CASE(scanner_record_position_includes_reindex_file_number)
{
    const fs::path path{m_path_root / "scanner-position.dat"};
    const CBlock block{MakeScanTestBlock(/*nonce=*/17)};

    {
        AutoFile file{fsbridge::fopen(path, "wb+")};
        BOOST_REQUIRE(!file.IsNull());
        WriteBlkRecord(file, Params(), block);
        CloseWrittenFile(file);
    }

    AutoFile file{fsbridge::fopen(path, "rb")};
    BOOST_REQUIRE(!file.IsNull());
    kernel::BlkFileScanner scanner{file, Params().MessageStart()};
    const kernel::BlkScanResult result{scanner.Next()};
    BOOST_REQUIRE(result.IsRecord());

    const FlatFilePos position{scanner.RecordPosition(/*file_number=*/42, result.record())};
    BOOST_CHECK_EQUAL(position.nFile, 42);
    BOOST_CHECK_EQUAL(position.nPos, result.record().block_position);
}

BOOST_AUTO_TEST_SUITE_END()
