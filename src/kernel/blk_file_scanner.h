// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_BLK_FILE_SCANNER_H
#define BITCOIN_KERNEL_BLK_FILE_SCANNER_H

#include <flatfile.h>
#include <kernel/messagestartchars.h>
#include <primitives/block.h>
#include <streams.h>
#include <uint256.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

class CBlock;

namespace util {
class SignalInterrupt;
} // namespace util

namespace kernel {

enum class BlkScanStatus {
    Record,
    Eof,
    MagicMismatch,
    ShortRead,
    DecodeError,
    OversizedRecord,
    IoError,
    Interrupted,
};

struct BlkRecord {
    uint64_t block_position{0};
    uint32_t payload_size{0};
    CBlockHeader header{};
    uint256 hash{};
};

class BlkScanResult
{
public:
    [[nodiscard]] static BlkScanResult Record(BlkRecord record)
    {
        const uint32_t payload_size{record.payload_size};
        return BlkScanResult{BlkScanStatus::Record, std::move(record), /*diagnostic_offset=*/0, payload_size, {}};
    }
    [[nodiscard]] static BlkScanResult Eof() { return BlkScanResult{BlkScanStatus::Eof, std::nullopt, /*diagnostic_offset=*/0, /*payload_size=*/0, {}}; }
    [[nodiscard]] static BlkScanResult MagicMismatch(uint64_t offset)
    {
        return BlkScanResult{BlkScanStatus::MagicMismatch, std::nullopt, offset, /*payload_size=*/0, "block magic mismatch"};
    }
    [[nodiscard]] static BlkScanResult ShortRead(uint64_t offset, std::string diagnostic)
    {
        return BlkScanResult{BlkScanStatus::ShortRead, std::nullopt, offset, /*payload_size=*/0, std::move(diagnostic)};
    }
    [[nodiscard]] static BlkScanResult DecodeError(uint64_t offset, std::string diagnostic)
    {
        return BlkScanResult{BlkScanStatus::DecodeError, std::nullopt, offset, /*payload_size=*/0, std::move(diagnostic)};
    }
    [[nodiscard]] static BlkScanResult OversizedRecord(uint64_t offset, uint32_t payload_size)
    {
        return BlkScanResult{BlkScanStatus::OversizedRecord, std::nullopt, offset, payload_size, "block payload exceeds maximum serialized size"};
    }
    [[nodiscard]] static BlkScanResult IoError(uint64_t offset, std::string diagnostic)
    {
        return BlkScanResult{BlkScanStatus::IoError, std::nullopt, offset, /*payload_size=*/0, std::move(diagnostic)};
    }
    [[nodiscard]] static BlkScanResult Interrupted(uint64_t offset)
    {
        return BlkScanResult{BlkScanStatus::Interrupted, std::nullopt, offset, /*payload_size=*/0, "block file scan interrupted"};
    }

    [[nodiscard]] BlkScanStatus status() const noexcept { return m_status; }
    [[nodiscard]] bool IsRecord() const noexcept { return m_status == BlkScanStatus::Record; }
    [[nodiscard]] bool IsEof() const noexcept { return m_status == BlkScanStatus::Eof; }
    [[nodiscard]] bool IsInterrupted() const noexcept { return m_status == BlkScanStatus::Interrupted; }
    [[nodiscard]] bool IsRecoverable() const noexcept
    {
        return m_status == BlkScanStatus::MagicMismatch ||
               m_status == BlkScanStatus::ShortRead ||
               m_status == BlkScanStatus::DecodeError ||
               m_status == BlkScanStatus::OversizedRecord;
    }
    [[nodiscard]] bool IsFatal() const noexcept { return m_status == BlkScanStatus::IoError; }

    [[nodiscard]] const BlkRecord& record() const
    {
        assert(m_status == BlkScanStatus::Record);
        assert(m_record.has_value());
        return *m_record;
    }
    [[nodiscard]] uint64_t diagnostic_offset() const noexcept { return m_diagnostic_offset; }
    [[nodiscard]] uint32_t payload_size() const noexcept { return m_payload_size; }
    [[nodiscard]] const std::string& diagnostic() const noexcept { return m_diagnostic; }

private:
    BlkScanResult(BlkScanStatus status, std::optional<BlkRecord> record, uint64_t diagnostic_offset, uint32_t payload_size, std::string diagnostic)
        : m_status{status},
          m_record{std::move(record)},
          m_diagnostic_offset{diagnostic_offset},
          m_payload_size{payload_size},
          m_diagnostic{std::move(diagnostic)}
    {
        assert(m_status == BlkScanStatus::Record || !m_record.has_value());
        assert(m_status != BlkScanStatus::Record || m_record.has_value());
    }

    BlkScanStatus m_status{BlkScanStatus::Eof};
    std::optional<BlkRecord> m_record;
    uint64_t m_diagnostic_offset{0};
    uint32_t m_payload_size{0};
    std::string m_diagnostic;
};

class BlkFileScanner
{
public:
    BlkFileScanner(AutoFile& file, MessageStartChars message_start, const util::SignalInterrupt* interrupt = nullptr);

    [[nodiscard]] BlkScanResult Next();
    [[nodiscard]] FlatFilePos RecordPosition(int file_number, const BlkRecord& record) const;
    void ReadBlock(CBlock& block, const BlkRecord& record);

private:
    [[nodiscard]] bool Interrupted() const noexcept;

    BufferedFile m_file;
    MessageStartChars m_message_start;
    const util::SignalInterrupt* m_interrupt{nullptr};
    uint64_t m_rewind{0};
};

} // namespace kernel

#endif // BITCOIN_KERNEL_BLK_FILE_SCANNER_H
