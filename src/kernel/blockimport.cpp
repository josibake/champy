// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/blockimport.h>

#include <flatfile.h>
#include <kernel/block_import_pipeline.h>
#include <kernel/cs_main.h>
#include <logging.h>
#include <kernel/blockstorage.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/translation.h>
#include <chainstate.h>

#include <atomic>
#include <map>
#include <span>
#include <utility>

namespace kernel {
namespace {

BlockImportResult ImportIOError(bilingual_str message)
{
    return util::Unexpected{BlockImportError{BlockImportErrorKind::IO, std::move(message)}};
}

BlockImportResult ImportChainstateError(bilingual_str message)
{
    return util::Unexpected{BlockImportError{BlockImportErrorKind::Chainstate, std::move(message)}};
}

BlockImportResult ImportChainstateError(const util::Result<void>& result)
{
    return ImportChainstateError(util::ErrorString(result));
}

void AccumulateImportOutcome(BlockImportOutcome& total, const BlockImportOutcome& part)
{
    total.counters.loaded_blocks += part.counters.loaded_blocks;
    total.counters.skipped_records += part.counters.skipped_records;
    total.counters.skipped_blocks += part.counters.skipped_blocks;
    total.counters.rejected_blocks += part.counters.rejected_blocks;
}

} // namespace

class ImportingNow
{
    std::atomic<bool>& m_importing;
    bool m_acquired{false};

public:
    ImportingNow(std::atomic<bool>& importing) : m_importing{importing}
    {
        m_acquired = !m_importing.exchange(true, std::memory_order_acq_rel);
    }
    ~ImportingNow()
    {
        if (m_acquired) {
            m_importing.store(false, std::memory_order_release);
        }
    }

    [[nodiscard]] bool acquired() const noexcept { return m_acquired; }
};

BlockImportResult ImportBlocks(ChainstateManager& chainman, std::span<const fs::path> import_paths, NodeSeconds current_time)
{
    ImportingNow imp{chainman.m_blockman.m_importing};
    if (!imp.acquired()) {
        return BlockImportOutcome{.status = BlockImportStatus::AlreadyImporting, .counters = {}};
    }
    BlockImportOutcome outcome;

    // -reindex
    if (!chainman.m_blockman.m_blockfiles_indexed) {
        int total_files{0};
        while (fs::exists(chainman.m_blockman.GetBlockPosFilename(FlatFilePos(total_files, 0)))) {
            total_files++;
        }

        // Map of disk positions for blocks with unknown parent (only used for reindex);
        // parent hash -> child disk position, multiple children can have the same parent.
        UnknownParentIndex blocks_with_unknown_parent;

        for (int nFile{0}; nFile < total_files; ++nFile) {
            FlatFilePos pos(nFile, 0);
            AutoFile file{chainman.m_blockman.OpenBlockFile(pos, /*fReadOnly=*/true)};
            if (file.IsNull()) {
                return ImportIOError(Untranslated(strprintf("failed to open block file %s for reindex", fs::PathToString(chainman.m_blockman.GetBlockPosFilename(pos)))));
            }
            LogInfo("Reindexing block file blk%05u.dat (%d%% complete)...", (unsigned int)nFile, nFile * 100 / total_files);
            auto result{ImportExternalBlockFile({
                .chainman = chainman,
                .file = file,
                .mode = ExternalBlockFileReindex{
                    .file_number = nFile,
                    .unknown_parent_index = blocks_with_unknown_parent,
                },
                .current_time = current_time,
            })};
            if (!result) {
                return util::Unexpected{result.error()};
            }
            AccumulateImportOutcome(outcome, *result);
            if (result->status != BlockImportStatus::Completed) {
                if (result->status == BlockImportStatus::Interrupted) LogInfo("Interrupt requested. Exit reindexing.");
                outcome.status = BlockImportStatus::Interrupted;
                if (result->status == BlockImportStatus::ResourceLimit) outcome.status = BlockImportStatus::ResourceLimit;
                return outcome;
            }
        }
        WITH_LOCK(::cs_main, chainman.m_blockman.m_block_tree_db->WriteReindexing(false));
        chainman.m_blockman.m_blockfiles_indexed = true;
        LogInfo("Reindexing finished");
        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        chainman.ActiveChainstate().LoadGenesisBlock();
    }

    // -loadblock=
    for (const fs::path& path : import_paths) {
        AutoFile file{fsbridge::fopen(path, "rb")};
        if (!file.IsNull()) {
            LogInfo("Importing blocks file %s...", fs::PathToString(path));
            auto result{ImportExternalBlockFile({
                .chainman = chainman,
                .file = file,
                .mode = ExternalBlockFileLoadBlock{},
                .current_time = current_time,
            })};
            if (!result) {
                return util::Unexpected{result.error()};
            }
            AccumulateImportOutcome(outcome, *result);
            if (result->status != BlockImportStatus::Completed) {
                if (result->status == BlockImportStatus::Interrupted) LogInfo("Interrupt requested. Exit block importing.");
                outcome.status = BlockImportStatus::Interrupted;
                if (result->status == BlockImportStatus::ResourceLimit) outcome.status = BlockImportStatus::ResourceLimit;
                return outcome;
            }
        } else {
            LogWarning("Could not open blocks file %s", fs::PathToString(path));
            return ImportIOError(Untranslated(strprintf("could not open blocks file %s", fs::PathToString(path))));
        }
    }

    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    if (auto result = chainman.ActivateBestChains(current_time); !result) {
        chainman.GetNotifications().fatalError(util::ErrorString(result));
        return ImportChainstateError(result);
    }
    // End scope of ImportingNow
    return outcome;
}

} // namespace kernel
