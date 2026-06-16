# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

include("${SOURCE_DIR}/cmake/BitcoinApiManifest.cmake")

function(require_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} is missing required kernel-boundary text: ${needle}")
  endif()
endfunction()

function(forbid_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(NOT match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} contains forbidden kernel-boundary text: ${needle}")
  endif()
endfunction()

require_text("cmake/BitcoinApiManifest.cmake" "bitcoin_kernel")
require_text("cmake/BitcoinApiManifest.cmake" "BITCOIN_API_EXPERIMENTAL_TARGETS")
require_text("cmake/BitcoinApiManifest.cmake" "BITCOIN_C_ABI_EXPERIMENTAL_TARGETS")
require_text("cmake/BitcoinApiManifest.cmake" "BITCOIN_C_ABI_SHIPPED_TARGETS")
require_text("cmake/BitcoinApiManifest.cmake" "BITCOIN_C_ABI_RELEASE_BLOCKING_TEXT")
require_text("cmake/BitcoinApiManifest.cmake" "btck_block_verify_result(")
require_text("cmake/BitcoinApiManifest.cmake" "bitcoin_validation_c")
require_text("cmake/script/CheckApiManifest.cmake" "bitcoinkernel cannot be a shipped C ABI target")
require_text("src/CMakeLists.txt" "add_library(bitcoin_kernel STATIC EXCLUDE_FROM_ALL")
require_text("src/CMakeLists.txt" "Build-tree-only draft: do not install or export bitcoin_kernel")
forbid_text("src/CMakeLists.txt" "install(TARGETS bitcoin_protocol bitcoin_validation bitcoin_kernel")
forbid_text("src/CMakeLists.txt" "install(TARGETS bitcoin_protocol bitcoin_validation bitcoin_validation_script_engine_objects")
forbid_text("src/CMakeLists.txt" "install(TARGETS bitcoin_protocol bitcoin_validation bitcoin_validation_private_script_engine")
require_text("src/kernel/CMakeLists.txt" "add_library(bitcoinkernel")
require_text("src/kernel/CMakeLists.txt" "Core's libbitcoinkernel component remains experimental.")
forbid_text("src/kernel/CMakeLists.txt" "add_library(bitcoin_kernel")
forbid_text("src/kernel/CMakeLists.txt" "add_library(bitcoin_validation_c")
require_text("src/bitcoin/kernel/api.h" "#include <bitcoin/kernel/chainstate.h>")
require_text("src/bitcoin/kernel/context.h" "class context")
require_text("src/bitcoin/kernel/chainstate.h" "class chainstate")
require_text("src/bitcoin/kernel/result.h" "class operation_error")

require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_Chainstate btck_Chainstate;")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockInfo")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_ChainSnapshot")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_Error")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_ChainstateRuntime")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockProcessResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_HeaderProcessResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockImportResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockReadResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockSpentOutputsReadResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockParseResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockHeaderParseResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_TransactionParseResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_BlockCheckResult")
require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_TransactionCheckResult")
require_text("src/kernel/bitcoinkernel.h" "typedef uint8_t btck_ParseStatus")
require_text("src/kernel/bitcoinkernel.h" "typedef uint8_t btck_CheckStatus")
require_text("src/kernel/bitcoinkernel.h" "typedef uint8_t btck_BlockProcessStatus")
require_text("src/kernel/bitcoinkernel.h" "typedef uint8_t btck_BlockReadStatus")
require_text("src/kernel/bitcoinkernel.h" "btck_transaction_parse_result")
require_text("src/kernel/bitcoinkernel.h" "btck_block_parse_result")
require_text("src/kernel/bitcoinkernel.h" "btck_block_header_parse_result")
require_text("src/kernel/bitcoinkernel.h" "btck_transaction_check_result")
require_text("src/kernel/bitcoinkernel.h" "btck_block_check_result")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_process_block_result")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_process_header_result")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_import_blocks_result")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_read_block_result")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_read_block_spent_outputs_result")
require_text("src/kernel/bitcoinkernel.h" "btck_tx_validation_state_copy")
require_text("src/kernel/bitcoinkernel.h" "btck_BlockImportStatus_RESOURCE_LIMIT")
require_text("src/kernel/bitcoinkernel.h" "btck_block_import_result_get_skipped_record_count")
require_text("src/kernel/bitcoinkernel.h" "btck_block_import_result_get_skipped_block_count")
require_text("src/kernel/bitcoinkernel.h" "btck_block_import_result_get_rejected_block_count")
require_text("src/kernel/blockimport.h" "AlreadyImporting")
forbid_text("src/kernel/blockimport.h" "BlockImportErrorKind::AlreadyImporting")
forbid_text("src/kernel/blockimport.cpp" "BlockImportErrorKind::AlreadyImporting")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_options_create")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_open")
require_text("src/kernel/bitcoinkernel.h" "btck_Error** error")
require_text("src/kernel/bitcoinkernel.h" "Validation-library C ABI declarations are build-tree experimental")
require_text("src/kernel/bitcoinkernel.h" "not a release ABI while they remain inside bitcoinkernel")
require_text("src/kernel/bitcoinkernel.h" "borrowed views valid only for the duration of the callback")
require_text("src/kernel/bitcoinkernel.h" "Validation interface callbacks preserve generated event order for each subscriber")
require_text("src/kernel/bitcoinkernel.h" "Event callbacks must return 0 on success and non-zero on failure.")
require_text("src/kernel/bitcoinkernel.h" "failure is an operational notification failure, not a validation result")
require_text("src/kernel/bitcoinkernel.h" "not a rollback signal")
require_text("src/kernel/bitcoinkernel.h" "State transitions completed before callback delivery")
require_text("src/kernel/bitcoinkernel.h" "must not call back into")
require_text("src/kernel/bitcoinkernel.h" "btck_ErrorCode_CALLBACK")
require_text("src/kernel/bitcoinkernel.h" "typedef int (*btck_NotifyHeaderTip)")
require_text("src/kernel/bitcoinkernel.h" "typedef int (*btck_ValidationInterfaceBlockChecked)")
require_text("src/kernel/bitcoinkernel.cpp" "class CallbackFailure")
require_text("src/kernel/bitcoinkernel.cpp" "KernelInvokeCallbackNoThrow")
require_text("src/kernel/bitcoinkernel.cpp" "SetOperationError(error, btck_ErrorCode_CALLBACK")
forbid_text("src/kernel/bitcoinkernel.cpp" "std::terminate()")
forbid_text("src/kernel/bitcoinkernel.cpp" "std::terminate(")
forbid_text("src/kernel/bitcoinkernel.h" "logs and terminates")
forbid_text("src/kernel/bitcoinkernel.h" "Current callback typedefs do not have a recoverable failure return")
require_text("src/kernel/bitcoinkernel_wrapper.h" "struct KernelNotifications")
require_text("src/kernel/bitcoinkernel_wrapper.h" "struct ValidationInterface")
require_text("src/kernel/bitcoinkernel_wrapper.h" "std::function<void")
require_text("src/kernel/bitcoinkernel_wrapper.h" "int callback_status")
require_text("src/kernel/bitcoinkernel_wrapper.h" "enum class BlockProcessStatus")
require_text("src/kernel/bitcoinkernel_wrapper.h" "struct BlockImportResult")
require_text("src/kernel/bitcoinkernel_wrapper.h" "RESOURCE_LIMIT = btck_BlockImportStatus_RESOURCE_LIMIT")
require_text("src/kernel/bitcoinkernel_wrapper.h" "btck_block_check_result")
require_text("src/kernel/bitcoinkernel_wrapper.h" "btck_chainstate_process_block_result")
require_text("src/kernel/bitcoinkernel_wrapper.h" "btck_chainstate_read_block_result")
require_text("src/kernel/bitcoinkernel.cpp" "#include <validation/chain_validation.h>")
require_text("src/kernel/bitcoinkernel.cpp" "struct BlockProcessResultValue")
require_text("src/kernel/bitcoinkernel.cpp" "btck_BlockProcessResult::create")
require_text("src/chainstate.h" "bool LoadChainTip(NodeSeconds current_time)")
require_text("src/chainstate.cpp" "bool Chainstate::LoadChainTip(NodeSeconds current_time)")
require_text("src/chainstate.cpp" "m_chainman.UpdateIBDStatus(current_time)")
require_text("src/chainstate.h" "bool PreciousBlock(BlockValidationState& state, NodeSeconds current_time")
require_text("src/chainstate.h" "bool InvalidateBlock(BlockValidationState& state, NodeSeconds current_time")
require_text("src/chainstate.cpp" "bool Chainstate::DisconnectTip(")
require_text("src/chainstate.cpp" "    NodeSeconds current_time,")
require_text("src/chainstate.cpp" "bool Chainstate::PreciousBlock(BlockValidationState& state, NodeSeconds current_time")
require_text("src/chainstate.cpp" "bool Chainstate::InvalidateBlock(BlockValidationState& state, NodeSeconds current_time")
require_text("src/validation/core_chain_activation.cpp" "DisconnectCoreChainToFork(active_chain, activation_fork.fork, request.resources.current_time")
require_text("src/kernel/chainstate_load.cpp" "chainstate.LoadChainTip(*options.current_time)")
require_text("src/test/kernel/test_kernel.cpp" "BOOST_AUTO_TEST_CASE(btck_validation_time_is_supplied_per_operation)")
require_text("src/test/kernel/test_kernel.cpp" "BOOST_AUTO_TEST_CASE(btck_chainstate_open_uses_supplied_time_for_loaded_tip)")
require_text("src/test/kernel/test_kernel.cpp" "BOOST_AUTO_TEST_CASE(btck_notification_callback_failure_is_reported)")
require_text("src/test/kernel/test_kernel.cpp" "BOOST_AUTO_TEST_CASE(btck_validation_interface_callback_failure_is_reported)")
require_text("src/test/kernel/test_kernel.cpp" "ErrorCode::CALLBACK")
require_text("src/test/kernel/CMakeLists.txt" "add_executable(test_kernel_api_consumer")
require_text("src/test/kernel/kernel_api_consumer.cpp" "#include <bitcoinkernel.h>")
require_text("src/test/kernel/CMakeLists.txt" "LABELS \"experimental_c_abi;non_release\"")
require_text("src/test/kernel/CMakeLists.txt" "add_executable(test_bitcoin_kernel_api_consumer")
require_text("src/test/kernel/bitcoin_kernel_api_consumer.cpp" "#include <bitcoin/kernel/api.h>")
require_text("src/test/kernel/CMakeLists.txt" "LABELS \"experimental_kernel;non_release\"")
require_text("src/test/kernel/CMakeLists.txt" "add_executable(test_c_api_validation_consumer")
require_text("src/test/kernel/c_api_validation_consumer.c" "#include <bitcoinkernel.h>")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_chainstate_runtime_set_current_time")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_block_validation_options_set_current_time")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_chainstate_process_header_result")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_chainstate_process_block_result")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_header_process_result_destroy")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_block_process_result_destroy")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_chainstate_destroy")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_ParseStatus_MALFORMED")
require_text("src/test/kernel/c_api_validation_consumer.c" "btck_BlockValidationResult_TIME_FUTURE")

file(GLOB_RECURSE kernel_sources
  "${SOURCE_DIR}/src/kernel/*.cpp"
  "${SOURCE_DIR}/src/kernel/*.h"
)

foreach(path IN LISTS kernel_sources)
  file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${path}")
  foreach(needle IN ITEMS
      "#include <node/"
      "namespace node"
      "node::"
      "CurrentNodeTime("
      "CurrentBlockValidationTime("
      "NodeClock::now("
      "GetTime(")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

foreach(header IN LISTS BITCOIN_KERNEL_PUBLIC_HEADERS)
  foreach(needle IN ITEMS
      "#include <chainstate.h>"
      "#include <chain.h>"
      "#include <coins.h>"
      "#include <consensus/"
      "#include <flatfile.h>"
      "#include <interfaces/"
      "#include <kernel/"
      "#include <logging.h>"
      "#include <node/"
      "#include <policy/"
      "#include <primitives/"
      "#include <serialize.h>"
      "#include <streams.h>"
      "#include <sync.h>"
      "#include <util/"
      "#include <validation.h>"
      "#include <validation/"
      "ArgsManager"
      "BlockManager"
      "BlockTreeEntry"
      "BlockValidationState"
      "CBlock"
      "CBlockHeader"
      "CBlockIndex"
      "CBlockUndo"
      "CCoinsView"
      "CCoinsViewCache"
      "ChainstateManager"
      "FlatFilePos"
      "LOCK("
      "LevelDB"
      "Mutex"
      "NodeClock"
      "cs_main"
      "gArgs"
      "btck_"
      "BITCOINKERNEL_"
      "extern \"C\"")
    forbid_text("src/${header}" "${needle}")
  endforeach()
endforeach()

forbid_text("src/kernel/CMakeLists.txt" "../node/")
forbid_text("src/kernel/chainstate_load.cpp" "m_blockman.LookupBlockIndex(")
forbid_text("src/chainstate.cpp" "bool Chainstate::LoadChainTip()")
foreach(needle IN ITEMS
    "UpdateIBDStatus(CurrentNodeTime())"
    "UpdateTip(pindexDelete->pprev, chain_events, CurrentNodeTime())"
    "ActivateBestChain(state, CurrentNodeTime()"
    "GuessVerificationProgress(to_mark_failed->pprev, CurrentNodeTime())"
    "DisconnectTip(state, repair_events)")
  forbid_text("src/chainstate.cpp" "${needle}")
endforeach()

foreach(relative_path IN ITEMS
    src/kernel/bitcoinkernel.cpp
    src/kernel/chainstate_load.cpp)
  foreach(needle IN ITEMS
      "CurrentNodeTime("
      "CurrentBlockValidationTime("
      "NodeClock::now("
      "GetTime(")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

foreach(needle IN ITEMS
    "BlockTreeEntry"
    "btck_BlockTreeEntry"
    "GetBlockTreeEntry"
    "GetBestEntry"
    "btck_ChainstateManager"
    "btck_chainstate_manager_"
    "btck_ErrorCode_PARSE"
    "CTxMemPool"
    "TxMemPool"
    "mempool"
    "Mempool"
    "node::"
    "#include <node/"
    "ChainstateMempoolSync"
    "ChainstateEventSink")
  forbid_text("src/kernel/bitcoinkernel.h" "${needle}")
endforeach()

foreach(needle IN ITEMS
    "class ChainMan"
    "ChainstateManagerOptions"
    "ChainstateManagerRuntimeOptions"
    "btck_chainstate_manager_"
    "btck_ErrorCode_PARSE"
    "ErrorCode::PARSE")
  forbid_text("src/kernel/bitcoinkernel_wrapper.h" "${needle}")
endforeach()

foreach(needle IN ITEMS
    "class KernelNotifications"
    "class ValidationInterface"
    "std::shared_ptr<T> notifications"
    "std::shared_ptr<T> validation_interface"
    "std::is_base_of_v<KernelNotifications"
    "std::is_base_of_v<ValidationInterface"
    "virtual void BlockChecked"
    "virtual void BlockTipHandler"
    "GetChain("
    "Transaction(std::span<const std::byte>"
    "BlockHeader(std::span<const std::byte>"
    "Block(const std::span<const std::byte>")
  forbid_text("src/kernel/bitcoinkernel_wrapper.h" "${needle}")
endforeach()

foreach(relative_path IN ITEMS
    src/kernel/bitcoinkernel.h
    src/kernel/bitcoinkernel.cpp
    src/kernel/bitcoinkernel_wrapper.h
    src/test/kernel/test_kernel.cpp)
  foreach(needle IN ITEMS
      "btck_transaction_parse("
      "btck_block_parse("
      "btck_block_header_parse("
      "btck_transaction_check("
      "btck_block_check("
      "btck_chainstate_process_header("
      "btck_chainstate_import_blocks("
      "btck_chainstate_process_block("
      "btck_chainstate_read_block("
      "btck_chainstate_read_block_spent_outputs(")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

foreach(relative_path IN ITEMS
    src/test/kernel/kernel_api_consumer.cpp
    src/test/kernel/bitcoin_kernel_api_consumer.cpp
    src/test/kernel/c_api_validation_consumer.c)
  foreach(needle IN ITEMS
      "#include <node/"
      "#include <chainstate.h>"
      "#include <validation/"
      "#include <kernel/bitcoinkernel_wrapper.h>")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()
