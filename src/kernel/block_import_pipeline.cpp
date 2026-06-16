// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/block_import_pipeline.h>

#include <chainstate.h>
#include <chainstate_event_sink.h>
#include <consensus/consensus.h>
#include <kernel/blk_file_scanner.h>
#include <kernel/blockstorage.h>
#include <kernel/cs_main.h>
#include <logging.h>
#include <primitives/block.h>
#include <sync.h>
#include <tinyformat.h>
#include <uint256.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_validation.h>
#include <validation/chain_validation.h>
#include <validation/runtime_time.h>
#include <validation_state.h>

#include <chrono>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace kernel {
namespace {

BlockImportResult ImportFailure(BlockImportErrorKind kind, bilingual_str message)
{
    return util::Unexpected{BlockImportError{kind, std::move(message)}};
}

enum class ImportAdmissionStatus {
    Stored,
    AlreadyKnown,
    Skipped,
    Rejected,
};

struct ImportAdmissionOutcome {
    ImportAdmissionStatus status{ImportAdmissionStatus::Skipped};
    bool opens_deferred_children{false};
};

using ImportAdmissionResult = util::Expected<ImportAdmissionOutcome, BlockImportError>;

class ImportReporter
{
public:
    explicit ImportReporter(ChainstateManager& chainman) : m_chainman{chainman} {}

    void RecoverableScanResult(const BlkScanResult& scan_result) const
    {
        LogDebug(BCLog::REINDEX, "%s: unexpected data at file offset 0x%x - %s. continuing\n", __func__, scan_result.diagnostic_offset(), scan_result.diagnostic());
    }

    void RecoverableDecodeError(uint64_t diagnostic_offset, const bilingual_str& diagnostic) const
    {
        LogDebug(BCLog::REINDEX, "%s: unexpected data at file offset 0x%x - %s. continuing\n", __func__, diagnostic_offset, diagnostic.original);
    }

    void NotifyHeaderTip() const { m_chainman.NotifyHeaderTip(); }

    void FatalSystemError(const std::runtime_error& e) const
    {
        m_chainman.GetNotifications().fatalError(strprintf(_("System error while loading external block file: %s"), e.what()));
    }

    void LoadedSummary(int loaded_blocks, SteadyClock::time_point start) const
    {
        LogInfo("Loaded %i blocks from external file in %dms", loaded_blocks, Ticks<std::chrono::milliseconds>(SteadyClock::now() - start));
    }

private:
    ChainstateManager& m_chainman;
};

class BlockRecordDecoder
{
public:
    explicit BlockRecordDecoder(BlkFileScanner& scanner) : m_scanner{scanner} {}

    [[nodiscard]] util::Expected<std::shared_ptr<CBlock>, bilingual_str> DecodeBlock(const BlkRecord& record)
    {
        auto block{std::make_shared<CBlock>()};
        try {
            m_scanner.ReadBlock(*block, record);
        } catch (const std::exception& e) {
            // Historical bugs added extra data to the block files that does not deserialize cleanly.
            return util::Unexpected{Untranslated(e.what())};
        }
        return block;
    }

private:
    BlkFileScanner& m_scanner;
};

class ImportAdmission
{
public:
    ImportAdmission(ChainstateManager& chainman, BlockValidationTime time) : m_chainman{chainman}, m_time{time} {}

    [[nodiscard]] ImportAdmissionResult AdmitBlock(const std::shared_ptr<CBlock>& block, const FlatFilePos* existing_block_pos) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        BlockValidationState state;
        const BlockAcceptanceResult acceptance{AcceptBlock({
            .chainman = m_chainman,
            .block = block,
            .state = state,
            .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .existing_block_pos = existing_block_pos, .header = {.min_pow_checked = true}},
            .time = m_time,
        })};

        if (state.IsError() || acceptance.IsStorageFailure()) {
            return util::Unexpected{BlockImportError{
                BlockImportErrorKind::Admission,
                Untranslated(strprintf("failed to accept imported block: %s", FormatValidationStateForLog(state)))}};
        }

        switch (acceptance.status()) {
        case BlockAcceptanceStatus::BlockDataStored:
            return ImportAdmissionOutcome{ImportAdmissionStatus::Stored, /*opens_deferred_children=*/true};
        case BlockAcceptanceStatus::BlockDataAlreadyKnown:
            return ImportAdmissionOutcome{ImportAdmissionStatus::AlreadyKnown, /*opens_deferred_children=*/true};
        case BlockAcceptanceStatus::BlockDataUnrequestedPreviouslyProcessed:
        case BlockAcceptanceStatus::BlockDataUnrequestedLessWorkThanTip:
        case BlockAcceptanceStatus::BlockDataUnrequestedTooFarAhead:
        case BlockAcceptanceStatus::BlockDataUnrequestedBelowMinimumChainWork:
            return ImportAdmissionOutcome{ImportAdmissionStatus::Skipped, /*opens_deferred_children=*/true};
        case BlockAcceptanceStatus::HeaderRejected:
        case BlockAcceptanceStatus::BlockRejected:
            return ImportAdmissionOutcome{ImportAdmissionStatus::Rejected, /*opens_deferred_children=*/false};
        case BlockAcceptanceStatus::StorageFailed:
            return util::Unexpected{BlockImportError{
                BlockImportErrorKind::Admission,
                Untranslated(strprintf("failed to store imported block: %s", FormatValidationStateForLog(state)))}};
        }
        Assume(false);
    }

private:
    ChainstateManager& m_chainman;
    BlockValidationTime m_time;
};

class ImportActivation
{
public:
    ImportActivation(ChainstateManager& chainman, CoreBlockDataStore& block_store)
        : m_chainman{chainman}, m_block_store{block_store}
    {
    }

    [[nodiscard]] BlockImportResult ActivateGenesisIfNeeded(const uint256& hash, NodeSeconds current_time) const
    {
        const CChainParams& params{m_chainman.GetParams()};
        if (hash != params.GetConsensus().hashGenesisBlock || WITH_LOCK(::cs_main, return m_chainman.ActiveHeight()) != -1) {
            return BlockImportOutcome{};
        }

        BlockValidationState state;
        if (!m_chainman.ActiveChainstate().ActivateBestChain(state, current_time, nullptr).Succeeded()) {
            return ImportFailure(BlockImportErrorKind::Activation, Untranslated(strprintf("failed to activate imported genesis block: %s", FormatValidationStateForLog(state))));
        }
        return BlockImportOutcome{};
    }

    [[nodiscard]] BlockImportResult ActivatePruneModeIfNeeded(const std::shared_ptr<CBlock>& stored_block, NodeSeconds current_time) const
    {
        if (!stored_block || !m_block_store.IsPruneMode() || !m_block_store.HasIndexedBlockFiles()) {
            return BlockImportOutcome{};
        }

        if (auto result{m_chainman.ActivateBestChains(current_time)}; !result) {
            LogDebug(BCLog::REINDEX, "%s\n", util::ErrorString(result).original);
            return ImportFailure(BlockImportErrorKind::Activation, util::ErrorString(result));
        }
        return BlockImportOutcome{};
    }

private:
    ChainstateManager& m_chainman;
    CoreBlockDataStore& m_block_store;
};

void RecordAdmissionOutcome(BlockImportOutcome& outcome, ImportAdmissionStatus status)
{
    switch (status) {
    case ImportAdmissionStatus::Stored:
        outcome.counters.loaded_blocks++;
        return;
    case ImportAdmissionStatus::AlreadyKnown:
    case ImportAdmissionStatus::Skipped:
        outcome.counters.skipped_blocks++;
        return;
    case ImportAdmissionStatus::Rejected:
        outcome.counters.rejected_blocks++;
        return;
    }
    Assume(false);
}

} // namespace

bool UnknownParentIndex::Add(const uint256& parent_hash, FlatFilePos child_pos)
{
    if (m_children.size() >= m_max_entries) return false;
    m_children.emplace(parent_hash, child_pos);
    return true;
}

std::vector<FlatFilePos> UnknownParentIndex::TakeChildrenOf(const uint256& parent_hash)
{
    std::vector<FlatFilePos> children;
    auto range{m_children.equal_range(parent_hash)};
    for (auto it{range.first}; it != range.second; ++it) {
        children.push_back(it->second);
    }
    m_children.erase(range.first, range.second);
    return children;
}

BlockImportResult ImportExternalBlockFile(const ExternalBlockFileImportRequest& request)
{
    ChainstateManager& chainman{request.chainman};
    const auto* reindex_mode{std::get_if<ExternalBlockFileReindex>(&request.mode)};
    const auto start{SteadyClock::now()};
    const BlockValidationTime current_validation_time{BlockValidationTime::FromCurrentTime(request.current_time)};
    const CChainParams& params{chainman.GetParams()};
    CoreBlockDataStore block_store{chainman.m_blockman};
    CoreBlockIndexStore block_index{chainman};
    ImportAdmission admission{chainman, current_validation_time};
    ImportActivation activation{chainman, block_store};
    ImportReporter reporter{chainman};

    BlockImportOutcome outcome;
    try {
        BlkFileScanner scanner{request.file, params.MessageStart(), &chainman.m_interrupt};
        BlockRecordDecoder decoder{scanner};
        while (true) {
            const BlkScanResult scan_result{scanner.Next()};
            if (scan_result.IsEof()) {
                break;
            }
            if (scan_result.IsInterrupted()) {
                outcome.status = BlockImportStatus::Interrupted;
                return outcome;
            }
            if (scan_result.IsFatal()) {
                return ImportFailure(BlockImportErrorKind::Scanner, Untranslated(strprintf("failed to scan external block file at offset 0x%x: %s", scan_result.diagnostic_offset(), scan_result.diagnostic())));
            }
            if (scan_result.IsRecoverable()) {
                reporter.RecoverableScanResult(scan_result);
                outcome.counters.skipped_records++;
                continue;
            }
            const BlkRecord& record{scan_result.record()};
            std::optional<FlatFilePos> record_pos;
            if (reindex_mode) record_pos = scanner.RecordPosition(reindex_mode->file_number, record);
            const CBlockHeader& header{record.header};
            const uint256& hash{record.hash};
            const uint64_t diagnostic_offset{record.block_position};
            const FlatFilePos* existing_block_pos{record_pos ? &*record_pos : nullptr};

            std::shared_ptr<CBlock> stored_block{}; // Keep available after cs_main is released for prune-mode activation.
            bool should_process{false};

            {
                LOCK(cs_main);
                // Detect out-of-order blocks and store them for later.
                if (hash != params.GetConsensus().hashGenesisBlock && !block_index.LookupBlockIndex(header.hashPrevBlock)) {
                    LogDebug(BCLog::REINDEX, "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                             header.hashPrevBlock.ToString());
                    if (reindex_mode) {
                        UnknownParentIndex& unknown_parent_index{reindex_mode->unknown_parent_index.get()};
                        if (!unknown_parent_index.Add(header.hashPrevBlock, *record_pos)) {
                            outcome.status = BlockImportStatus::ResourceLimit;
                            return outcome;
                        }
                    }
                    continue;
                }

                // Process in case the block isn't known yet.
                const CBlockIndex* pindex = block_index.LookupBlockIndex(hash);
                if (!pindex || (pindex->nStatus & BLOCK_HAVE_DATA) == 0) {
                    should_process = true;
                } else if (hash != params.GetConsensus().hashGenesisBlock && pindex->nHeight % 1000 == 0) {
                    LogDebug(BCLog::REINDEX, "Block Import: already had block %s at height %d\n", hash.ToString(), pindex->nHeight);
                    outcome.counters.skipped_blocks++;
                }
            }

            if (should_process) {
                // This block can be processed immediately; rewind to its start, read and deserialize it.
                auto decoded{decoder.DecodeBlock(record)};
                if (!decoded) {
                    reporter.RecoverableDecodeError(diagnostic_offset, decoded.error());
                    outcome.counters.skipped_records++;
                    continue;
                }
                std::shared_ptr<CBlock> pblock{*decoded};

                ImportAdmissionOutcome admission_outcome;
                {
                    LOCK(cs_main);
                    const CBlockIndex* pindex = block_index.LookupBlockIndex(hash);
                    if (!pindex || (pindex->nStatus & BLOCK_HAVE_DATA) == 0) {
                        auto admission_result{admission.AdmitBlock(pblock, existing_block_pos)};
                        if (!admission_result) return util::Unexpected{std::move(admission_result.error())};
                        admission_outcome = *admission_result;
                    } else {
                        admission_outcome = {ImportAdmissionStatus::AlreadyKnown, /*opens_deferred_children=*/true};
                    }
                }
                RecordAdmissionOutcome(outcome, admission_outcome.status);
                if (admission_outcome.status == ImportAdmissionStatus::Stored) stored_block = std::move(pblock);
            }

            // Activate the genesis block so normal node progress can continue.
            // During first -reindex, this will only connect Genesis since
            // ActivateBestChain only connects blocks which are in the block tree db,
            // which only contains blocks whose parents are in it.
            // But do this only if genesis isn't activated yet, to avoid connecting many blocks
            // without assumevalid in the case of a continuation of a reindex that
            // was interrupted by the user.
            if (auto result{activation.ActivateGenesisIfNeeded(hash, request.current_time)}; !result) return result;
            // Must update the tip for pruning to work while importing with -loadblock.
            if (auto result{activation.ActivatePruneModeIfNeeded(stored_block, request.current_time)}; !result) return result;

            reporter.NotifyHeaderTip();

            if (!reindex_mode) continue;

            // Recursively process earlier encountered successors of this block.
            UnknownParentIndex& unknown_parent_index{reindex_mode->unknown_parent_index.get()};
            std::deque<uint256> queue;
            queue.push_back(hash);
            while (!queue.empty()) {
                if (chainman.m_interrupt) {
                    outcome.status = BlockImportStatus::Interrupted;
                    return outcome;
                }
                const uint256 head{queue.front()};
                queue.pop_front();
                for (const FlatFilePos child_pos : unknown_parent_index.TakeChildrenOf(head)) {
                    std::shared_ptr<CBlock> pblockrecursive = std::make_shared<CBlock>();
                    auto recursive_block{block_store.ReadBlockFromPosition(child_pos, {})};
                    if (!recursive_block) {
                        return ImportFailure(BlockImportErrorKind::Read, Untranslated(strprintf("failed to read deferred child block at %s", child_pos.ToString())));
                    }
                    *pblockrecursive = std::move(*recursive_block);
                    const auto& block_hash{pblockrecursive->GetHash()};
                    LogDebug(BCLog::REINDEX, "%s: Processing out of order child %s of %s", __func__, block_hash.ToString(), head.ToString());
                    ImportAdmissionOutcome admission_outcome;
                    {
                        LOCK(cs_main);
                        auto admission_result{admission.AdmitBlock(pblockrecursive, &child_pos)};
                        if (!admission_result) return util::Unexpected{std::move(admission_result.error())};
                        admission_outcome = *admission_result;
                    }
                    RecordAdmissionOutcome(outcome, admission_outcome.status);
                    if (admission_outcome.opens_deferred_children) queue.push_back(block_hash);
                    reporter.NotifyHeaderTip();
                }
            }
        }
    } catch (const std::runtime_error& e) {
        reporter.FatalSystemError(e);
        return ImportFailure(BlockImportErrorKind::Chainstate, strprintf(_("System error while loading external block file: %s"), e.what()));
    }
    reporter.LoadedSummary(outcome.counters.loaded_blocks, start);
    return outcome;
}

} // namespace kernel
