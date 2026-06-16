// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/blk_file_scanner.h>

#include <consensus/consensus.h>
#include <serialize.h>
#include <tinyformat.h>
#include <util/signalinterrupt.h>

#include <cstddef>
#include <exception>
#include <ios>
#include <limits>
#include <string_view>

namespace kernel {
namespace {

bool IsEndOfFileException(const std::exception& e)
{
    return std::string_view{e.what()}.find("end of file") != std::string_view::npos;
}

BlkScanResult ShortReadOrIoError(uint64_t offset, const std::exception& e)
{
    if (IsEndOfFileException(e)) return BlkScanResult::ShortRead(offset, e.what());
    return BlkScanResult::IoError(offset, e.what());
}

BlkScanResult EofOrIoError(uint64_t offset, const std::exception& e)
{
    if (IsEndOfFileException(e)) return BlkScanResult::Eof();
    return BlkScanResult::IoError(offset, e.what());
}

} // namespace

BlkFileScanner::BlkFileScanner(AutoFile& file, MessageStartChars message_start, const util::SignalInterrupt* interrupt)
    : m_file{file, 2 * MAX_BLOCK_SERIALIZED_SIZE, MAX_BLOCK_SERIALIZED_SIZE + 8},
      m_message_start{message_start},
      m_interrupt{interrupt},
      m_rewind{m_file.GetPos()}
{
}

bool BlkFileScanner::Interrupted() const noexcept
{
    return m_interrupt && static_cast<bool>(*m_interrupt);
}

BlkScanResult BlkFileScanner::Next()
{
    while (!m_file.eof()) {
        if (Interrupted()) return BlkScanResult::Interrupted(m_file.GetPos());
        m_file.SetPos(m_rewind);
        m_rewind++; // Start one byte further next time, in case of failure.
        m_file.SetLimit();
        uint32_t size{0};
        uint64_t marker_position{m_file.GetPos()};
        bool found_marker{false};
        try {
            MessageStartChars marker;
            if (!m_file.FindByte(std::byte{m_message_start[0]}, [&] { return Interrupted(); })) {
                return BlkScanResult::Interrupted(m_file.GetPos());
            }
            marker_position = m_file.GetPos();
            found_marker = true;
            m_rewind = m_file.GetPos() + 1;
            m_file >> marker;
            if (marker != m_message_start) return BlkScanResult::MagicMismatch(marker_position);
            m_file >> size;
            if (size < 80) {
                return BlkScanResult::DecodeError(marker_position, strprintf("block payload too small: %u bytes", size));
            }
            if (size > MAX_BLOCK_SERIALIZED_SIZE) return BlkScanResult::OversizedRecord(marker_position, size);
        } catch (const std::exception& e) {
            // EOF before the first magic byte is ordinary end-of-file; EOF
            // after a marker starts is a recoverable truncated record.
            if (!found_marker) return EofOrIoError(marker_position, e);
            return ShortReadOrIoError(marker_position, e);
        }

        try {
            const uint64_t block_position{m_file.GetPos()};
            m_file.SetLimit(block_position + size);
            CBlockHeader header;
            m_file >> header;
            const uint256 hash{header.GetHash()};
            m_rewind = block_position + size;
            m_file.SkipTo(m_rewind);
            return BlkScanResult::Record({
                .block_position = block_position,
                .payload_size = size,
                .header = header,
                .hash = hash,
            });
        } catch (const std::exception& e) {
            return ShortReadOrIoError(marker_position, e);
        }
    }
    return BlkScanResult::Eof();
}

FlatFilePos BlkFileScanner::RecordPosition(int file_number, const BlkRecord& record) const
{
    assert(record.block_position <= std::numeric_limits<uint32_t>::max());
    return FlatFilePos{file_number, static_cast<unsigned int>(record.block_position)};
}

void BlkFileScanner::ReadBlock(CBlock& block, const BlkRecord& record)
{
    if (!m_file.SetPos(record.block_position)) {
        throw std::ios_base::failure{"BlkFileScanner::ReadBlock: failed to seek to record"};
    }
    if (!m_file.SetLimit(record.block_position + record.payload_size)) {
        throw std::ios_base::failure{"BlkFileScanner::ReadBlock: failed to set record limit"};
    }
    try {
        m_file >> TX_WITH_WITNESS(block);
        m_rewind = m_file.GetPos();
        m_file.SetLimit();
    } catch (...) {
        m_file.SetLimit();
        throw;
    }
}

} // namespace kernel
