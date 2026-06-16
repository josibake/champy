// Copyright (c) 2020-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <clientversion.h>
#include <flatfile.h>
#include <kernel/block_import_pipeline.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <test/util/setup_common.h>
#include <test/util/time.h>
#include <util/time.h>
#include <validation/runtime_time.h>
#include <chainstate.h>

#include <cstdint>
#include <vector>

namespace {
const TestingSetup* g_setup;
} // namespace

void initialize_load_external_block_file()
{
    static const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    g_setup = testing_setup.get();
}

FUZZ_TARGET(load_external_block_file, .init = initialize_load_external_block_file)
{
    FuzzedDataProvider fuzzed_data_provider{buffer.data(), buffer.size()};
    NodeClockContext clock_ctx{ConsumeTime(fuzzed_data_provider)};
    FuzzedFileProvider fuzzed_file_provider{fuzzed_data_provider};
    AutoFile fuzzed_block_file{fuzzed_file_provider.open()};
    if (fuzzed_block_file.IsNull()) {
        return;
    }
    if (fuzzed_data_provider.ConsumeBool()) {
        // Corresponds to the -reindex case (track orphan blocks across files).
        kernel::UnknownParentIndex blocks_with_unknown_parent;
        (void)kernel::ImportExternalBlockFile({
            .chainman = *g_setup->m_node.chainman,
            .file = fuzzed_block_file,
            .mode = kernel::ExternalBlockFileReindex{
                .file_number = 0,
                .unknown_parent_index = blocks_with_unknown_parent,
            },
            .current_time = CurrentNodeTime(),
        });
    } else {
        // Corresponds to the -loadblock= case (orphan blocks aren't tracked across files).
        (void)kernel::ImportExternalBlockFile({
            .chainman = *g_setup->m_node.chainman,
            .file = fuzzed_block_file,
            .mode = kernel::ExternalBlockFileLoadBlock{},
            .current_time = CurrentNodeTime(),
        });
    }
}
