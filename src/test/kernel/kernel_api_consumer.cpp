// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoinkernel.h>

#include <cstddef>
#include <cstdint>

int main()
{
    btck_Context* context{nullptr};
    btck_ContextOptions* context_options{nullptr};
    btck_ChainParameters* chain_parameters{nullptr};
    btck_Chainstate* chainman{nullptr};
    btck_ChainstateOptions* chainman_options{nullptr};
    btck_BlockInfo* block_info{nullptr};
    btck_ChainSnapshot* chain_snapshot{nullptr};
    btck_BlockProcessResult* block_process_result{nullptr};
    btck_HeaderProcessResult* header_process_result{nullptr};
    btck_BlockImportResult* block_import_result{nullptr};
    btck_BlockReadResult* block_read_result{nullptr};
    btck_BlockSpentOutputsReadResult* spent_outputs_read_result{nullptr};
    btck_BlockParseResult* block_parse_result{nullptr};
    btck_BlockHeaderParseResult* header_parse_result{nullptr};
    btck_TransactionParseResult* transaction_parse_result{nullptr};
    btck_BlockCheckResult* block_check_result{nullptr};
    btck_TransactionCheckResult* transaction_check_result{nullptr};
    btck_ParseStatus parse_status{btck_ParseStatus_MALFORMED};
    btck_BlockProcessStatus block_process_status{btck_BlockProcessStatus_CHECK_FAILED};
    btck_BlockImportStatus block_import_status{btck_BlockImportStatus_RESOURCE_LIMIT};
    auto skipped_record_accessor{&btck_block_import_result_get_skipped_record_count};
    auto skipped_block_accessor{&btck_block_import_result_get_skipped_block_count};
    auto rejected_block_accessor{&btck_block_import_result_get_rejected_block_count};

    return context == nullptr &&
                   context_options == nullptr &&
                   chain_parameters == nullptr &&
                   chainman == nullptr &&
                   chainman_options == nullptr &&
                   block_info == nullptr &&
                   chain_snapshot == nullptr &&
                   block_process_result == nullptr &&
                   header_process_result == nullptr &&
                   block_import_result == nullptr &&
                   block_read_result == nullptr &&
                   spent_outputs_read_result == nullptr &&
                   block_parse_result == nullptr &&
                   header_parse_result == nullptr &&
                   transaction_parse_result == nullptr &&
                   block_check_result == nullptr &&
                   transaction_check_result == nullptr &&
                   parse_status == btck_ParseStatus_MALFORMED &&
                   block_process_status == btck_BlockProcessStatus_CHECK_FAILED &&
                   block_import_status == btck_BlockImportStatus_RESOURCE_LIMIT &&
                   skipped_record_accessor != nullptr &&
                   skipped_block_accessor != nullptr &&
                   rejected_block_accessor != nullptr ?
               0 :
               1;
}
