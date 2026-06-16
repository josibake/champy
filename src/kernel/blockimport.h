// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_BLOCKIMPORT_H
#define BITCOIN_KERNEL_BLOCKIMPORT_H

#include <util/expected.h>
#include <util/fs.h>
#include <util/time.h>
#include <util/translation.h>

#include <span>

class ChainstateManager;

namespace kernel {

enum class BlockImportErrorKind {
    IO,
    Scanner,
    Admission,
    Activation,
    Read,
    Flush,
    Chainstate,
};

enum class BlockImportStatus {
    Completed,
    Interrupted,
    ResourceLimit,
    AlreadyImporting,
};

struct BlockImportError {
    BlockImportErrorKind kind;
    bilingual_str message;
};

struct BlockImportCounters {
    int loaded_blocks{0};
    int skipped_records{0};
    int skipped_blocks{0};
    int rejected_blocks{0};
};

struct BlockImportOutcome {
    BlockImportStatus status{BlockImportStatus::Completed};
    BlockImportCounters counters;
};

using BlockImportResult = util::Expected<BlockImportOutcome, BlockImportError>;

// Calls ActivateBestChain() even if no blocks are imported.
BlockImportResult ImportBlocks(ChainstateManager& chainman, std::span<const fs::path> import_paths, NodeSeconds current_time);

} // namespace kernel

#endif // BITCOIN_KERNEL_BLOCKIMPORT_H
