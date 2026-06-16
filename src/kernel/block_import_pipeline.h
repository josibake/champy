// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_BLOCK_IMPORT_PIPELINE_H
#define BITCOIN_KERNEL_BLOCK_IMPORT_PIPELINE_H

#include <flatfile.h>
#include <kernel/blockimport.h>
#include <streams.h>
#include <uint256.h>
#include <util/result.h>
#include <util/time.h>

#include <cstddef>
#include <functional>
#include <map>
#include <variant>
#include <vector>

class ChainstateManager;

namespace kernel {

class UnknownParentIndex
{
public:
    static constexpr size_t DEFAULT_MAX_ENTRIES{1'000'000};

    explicit UnknownParentIndex(size_t max_entries = DEFAULT_MAX_ENTRIES) noexcept
        : m_max_entries{max_entries}
    {
    }

    [[nodiscard]] bool Add(const uint256& parent_hash, FlatFilePos child_pos);
    [[nodiscard]] std::vector<FlatFilePos> TakeChildrenOf(const uint256& parent_hash);
    [[nodiscard]] bool Empty() const noexcept { return m_children.empty(); }
    [[nodiscard]] size_t Size() const noexcept { return m_children.size(); }
    [[nodiscard]] size_t MaxEntries() const noexcept { return m_max_entries; }

private:
    size_t m_max_entries{DEFAULT_MAX_ENTRIES};
    std::multimap<uint256, FlatFilePos> m_children;
};

struct ExternalBlockFileLoadBlock {
};

struct ExternalBlockFileReindex {
    int file_number{0};
    std::reference_wrapper<UnknownParentIndex> unknown_parent_index;
};

using ExternalBlockFileImportMode = std::variant<ExternalBlockFileLoadBlock, ExternalBlockFileReindex>;

struct ExternalBlockFileImportRequest {
    ChainstateManager& chainman;
    AutoFile& file;
    ExternalBlockFileImportMode mode{ExternalBlockFileLoadBlock{}};
    NodeSeconds current_time;
};

[[nodiscard]] BlockImportResult ImportExternalBlockFile(const ExternalBlockFileImportRequest& request);

} // namespace kernel

#endif // BITCOIN_KERNEL_BLOCK_IMPORT_PIPELINE_H
