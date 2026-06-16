# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

include("${SOURCE_DIR}/cmake/BitcoinApiManifest.cmake")

function(require_file relative_path)
  if(NOT EXISTS "${SOURCE_DIR}/${relative_path}")
    message(FATAL_ERROR "Expected ${relative_path} to exist")
  endif()
endfunction()

function(forbid_file relative_path)
  if(EXISTS "${SOURCE_DIR}/${relative_path}")
    message(FATAL_ERROR "Forbidden validation-library boundary path still exists: ${relative_path}")
  endif()
endfunction()

function(require_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} is missing required validation-library-boundary text: ${needle}")
  endif()
endfunction()

function(forbid_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(NOT match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} contains forbidden validation-library-boundary text: ${needle}")
  endif()
endfunction()

function(extract_cmake_call_from_file out_var cmake_file call_name target_name)
  file(READ "${cmake_file}" cmake_contents)
  set(call_start -1)
  foreach(call_prefix IN ITEMS
      "${call_name}(${target_name}\n"
      "${call_name}(${target_name} "
      "${call_name}(${target_name}\t"
      "${call_name}(${target_name}\r"
      "${call_name}(${target_name})")
    string(FIND "${cmake_contents}" "${call_prefix}" candidate_start)
    if(NOT candidate_start EQUAL -1)
      set(call_start "${candidate_start}")
      break()
    endif()
  endforeach()
  if(call_start EQUAL -1)
    message(FATAL_ERROR "Could not find ${call_name}(${target_name}...) in ${cmake_file}")
  endif()

  string(SUBSTRING "${cmake_contents}" ${call_start} -1 call_tail)
  string(FIND "${call_tail}" "\n)" call_end)
  if(call_end EQUAL -1)
    message(FATAL_ERROR "Could not find end of ${call_name}(${target_name}...) in ${cmake_file}")
  endif()

  math(EXPR call_length "${call_end} + 2")
  string(SUBSTRING "${call_tail}" 0 ${call_length} call_block)
  set(${out_var} "${call_block}" PARENT_SCOPE)
endfunction()

function(require_target_link_allowlist target_name)
  set(options ALLOW_MISSING)
  set(one_value_args CMAKE_FILE)
  set(multi_value_args ALLOWED)
  cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
  if(ARG_ALLOW_MISSING)
    file(READ "${ARG_CMAKE_FILE}" cmake_contents)
    set(call_start -1)
    foreach(call_prefix IN ITEMS
        "target_link_libraries(${target_name}\n"
        "target_link_libraries(${target_name} "
        "target_link_libraries(${target_name}\t"
        "target_link_libraries(${target_name}\r"
        "target_link_libraries(${target_name})")
      string(FIND "${cmake_contents}" "${call_prefix}" candidate_start)
      if(NOT candidate_start EQUAL -1)
        set(call_start "${candidate_start}")
        break()
      endif()
    endforeach()
    if(call_start EQUAL -1)
      return()
    endif()
  endif()
  extract_cmake_call_from_file(target_links_block "${ARG_CMAKE_FILE}" "target_link_libraries" "${target_name}")
  string(REGEX MATCHALL "[A-Za-z0-9_:.-]+" target_link_tokens "${target_links_block}")
  foreach(token IN LISTS target_link_tokens)
    if(token STREQUAL "target_link_libraries"
        OR token STREQUAL "${target_name}"
        OR token STREQUAL "PRIVATE"
        OR token STREQUAL "PUBLIC"
        OR token STREQUAL "INTERFACE")
      continue()
    endif()

    list(FIND ARG_ALLOWED "${token}" allowed_link_index)
    if(allowed_link_index EQUAL -1)
      message(FATAL_ERROR "${target_name} links forbidden validation-library dependency ${token}")
    endif()
  endforeach()
endfunction()

foreach(relative_path IN ITEMS
    src/bitcoin/api.h
    src/bitcoin/chain_graph/chain_graph.cpp
    src/bitcoin/chain_graph/chain_graph.h
    src/bitcoin/core_adapter/api.h
    src/bitcoin/core_adapter/block.cpp
    src/bitcoin/core_adapter/block.h
    src/bitcoin/protocol/amount.h
    src/bitcoin/protocol/api.h
    src/bitcoin/protocol/block.h
    src/bitcoin/protocol/block_header.h
    src/bitcoin/protocol/chain_view.h
    src/bitcoin/protocol/codec.cpp
    src/bitcoin/protocol/codec.h
    src/bitcoin/protocol/coin_index.h
    src/bitcoin/protocol/detail/byte_reader.h
    src/bitcoin/protocol/detail/compact_size.h
    src/bitcoin/protocol/detail/endian.h
    src/bitcoin/protocol/detail/hash_writer.h
    src/bitcoin/protocol/hash.h
    src/bitcoin/protocol/result.h
    src/bitcoin/protocol/script.h
    src/bitcoin/protocol/transaction.h
    src/bitcoin/validation/api.h
    src/bitcoin/validation/block.cpp
    src/bitcoin/validation/block.h
    src/bitcoin/validation/context.h
    src/bitcoin/validation/header.cpp
    src/bitcoin/validation/header.h
    src/bitcoin/validation/result.h
    src/bitcoin/validation/rules.h
    src/bitcoin/validation/script.cpp
    src/bitcoin/validation/script.h
    src/bitcoin/validation/spend.cpp
    src/bitcoin/validation/spend.h
    src/bitcoin/validation/transaction.cpp
    src/bitcoin/validation/transaction.h
    src/bitcoin/validation/verify.cpp
    src/bitcoin/validation/verify.h
    src/test/bitcoin_validation_block_tests.cpp
    src/test/bitcoin_validation_header_tests.cpp
    src/test/bitcoin_validation_installed_package_consumer/CMakeLists.txt
    src/test/bitcoin_validation_installed_package_consumer/main.cpp
    src/test/bitcoin_validation_script_tests.cpp
    src/test/bitcoin_validation_spend_tests.cpp
    src/test/bitcoin_validation_transaction_tests.cpp
    src/test/bitcoin_validation_rule_inventory_tests.cpp
    src/test/bitcoin_validation_core_adapter_consumer.cpp
    src/test/bitcoin_chain_graph_invariant_tests.cpp
    src/test/bitcoin_chain_graph_model_tests.cpp
    src/test/bitcoin_chain_graph_tests.cpp
    src/test/bitcoin_chain_view_tests.cpp
    src/test/bitcoin_coin_index_tests.cpp
    src/test/bitcoin_protocol_codec_tests.cpp
    src/test/bitcoin_protocol_result_tests.cpp
    src/test/bitcoin_protocol_vocabulary_tests.cpp
    src/test/hornet_style_validation_consumer.cpp
    src/test/libbitcoin_style_validation_consumer.cpp
    src/test/std_bitcoin_style_validation_consumer.cpp
    src/test/validation_library_result_tests.cpp
    src/test/validation_library_api_consumer.cpp)
  require_file("${relative_path}")
endforeach()

forbid_file("doc")

foreach(relative_path IN ITEMS
    src/bitcoin/amount.h
    src/bitcoin/block.h
    src/bitcoin/chain.h
    src/bitcoin/codec.h
    src/bitcoin/hash.h
    src/bitcoin/result.h
    src/bitcoin/script.h
    src/bitcoin/transaction.h
    src/bitcoin/validation.h
    src/validation/block_candidate_admission.h
    src/validation/core_block_candidate_admission.cpp
    src/validation/core_block_candidate_admission.h
    src/validation/core_validation_committer.cpp
    src/validation/core_validation_committer.h
    src/validation/ibd_segment_adapters.cpp
    src/validation/ibd_segment_adapters.h
    src/validation/in_memory_segment_runtime.cpp
    src/validation/in_memory_segment_runtime.h
    src/validation/script_plan_executor.cpp
    src/validation/script_plan_executor.h
    src/validation/segment_validation_executor.cpp
    src/validation/segment_validation_executor.h
    src/validation/validation_committer.h
    src/validation/validation_facade.cpp
    src/validation/validation_facade.h
    src/validation/validation_pipeline.cpp
    src/validation/validation_pipeline.h
    src/validation/validation_segment_types.h
    src/node/ibd_block_candidate.h
    src/node/ibd_candidate_chain_overlay.cpp
    src/node/ibd_candidate_chain_overlay.h
    src/node/ibd_core_backend.cpp
    src/node/ibd_core_backend.h
    src/node/ibd_pipeline.h
    src/node/ibd_pipeline_controller.cpp
    src/node/ibd_pipeline_controller.h
    src/node/ibd_segment_executor.h
    src/node/ibd_segment_job.h
    src/node/ibd_segment_scheduler.cpp
    src/node/ibd_segment_scheduler.h
    src/node/ibd_segment_worker_executor.cpp
    src/node/ibd_segment_worker_executor.h
    src/node/ibd_validated_block.h
    src/test/ibd_pipeline_tests.cpp
    src/bench/ibd_pipeline.cpp)
  forbid_file("${relative_path}")
endforeach()

require_text("src/CMakeLists.txt" "add_library(bitcoin_protocol STATIC EXCLUDE_FROM_ALL")
require_text("src/CMakeLists.txt" "add_library(bitcoin_chain_graph STATIC EXCLUDE_FROM_ALL")
require_text("src/CMakeLists.txt" "add_library(bitcoin_validation_core_adapter STATIC EXCLUDE_FROM_ALL")
require_text("src/CMakeLists.txt" "add_library(bitcoin_validation STATIC EXCLUDE_FROM_ALL")
require_text("src/CMakeLists.txt" "add_library(bitcoin_validation_script_engine_objects OBJECT")
require_text("src/CMakeLists.txt" "bitcoin/chain_graph/chain_graph.cpp")
require_text("src/CMakeLists.txt" "bitcoin/core_adapter/block.cpp")
require_text("src/CMakeLists.txt" "bitcoin/validation/block.cpp")
require_text("src/CMakeLists.txt" "bitcoin/validation/header.cpp")
require_text("src/CMakeLists.txt" "bitcoin/validation/script.cpp")
require_text("src/CMakeLists.txt" "bitcoin/validation/spend.cpp")
require_text("src/CMakeLists.txt" "bitcoin/validation/transaction.cpp")
require_text("src/CMakeLists.txt" "target_link_libraries(bitcoin_validation")
require_text("src/CMakeLists.txt" "BitcoinApiManifest.cmake")
require_text("src/CMakeLists.txt" "install(TARGETS bitcoin_protocol bitcoin_validation")
require_text("src/CMakeLists.txt" "EXPORT BitcoinValidationTargets")
require_text("src/CMakeLists.txt" "BitcoinValidationConfig.cmake")
require_text("src/CMakeLists.txt" "$<TARGET_OBJECTS:bitcoin_validation_script_engine_objects>")
forbid_text("src/CMakeLists.txt" "bitcoin_validation_private_script_engine")
forbid_text("src/CMakeLists.txt" "install(TARGETS bitcoin_protocol bitcoin_validation bitcoin_validation_script_engine_objects")
require_text("src/CMakeLists.txt" "crypto/ripemd160.cpp")
require_text("src/CMakeLists.txt" "crypto/sha1.cpp")
require_text("src/CMakeLists.txt" "crypto/sha256.cpp")
require_text("cmake/BitcoinApiManifest.cmake" "BITCOIN_C_ABI_EXPERIMENTAL_TARGETS")
forbid_text("src/CMakeLists.txt" "install(TARGETS bitcoin_protocol bitcoin_validation bitcoin_validation_private_script_engine bitcoinkernel")
forbid_text("src/CMakeLists.txt" "BITCOIN_BITCOINKERNEL_PUBLIC_HEADERS")
forbid_text("cmake/BitcoinValidationConfig.cmake.in" "bitcoinkernel")
forbid_text("cmake/BitcoinValidationConfig.cmake.in" "kernel/bitcoinkernel")
forbid_text("src/CMakeLists.txt" "$<BUILD_INTERFACE:bitcoin_consensus>")
require_text("src/bitcoin/validation/result.h" "data_unavailable")
require_text("src/bitcoin/protocol/api.h" "#include <bitcoin/protocol/coin_index.h>")
require_text("src/bitcoin/api.h" "#include <bitcoin/chain_graph/chain_graph.h>")
require_text("src/bitcoin/validation/api.h" "#include <bitcoin/validation/block.h>")
require_text("src/bitcoin/validation/api.h" "#include <bitcoin/validation/spend.h>")
require_text("src/bitcoin/protocol/chain_view.h" "std::remove_cvref_t")
require_text("src/bitcoin/protocol/coin_index.h" "enum class coin_lookup_state")
require_text("src/bitcoin/protocol/coin_index.h" "enum class coin_lookup_failure")
require_text("src/bitcoin/validation/context.h" "class verification_flags")
require_text("src/bitcoin/validation/script.h" "class script_execution_result")
require_text("src/bitcoin/validation/script.h" "verify_script")
forbid_text("src/bitcoin/validation/script.h" "concept script_verifier")
forbid_text("src/bitcoin/validation/script.h" "script_input")
forbid_text("src/bitcoin/validation/script.h" "script_check_batch")
require_text("src/bitcoin/validation/block.h" "verify(const block& candidate)")
require_text("src/bitcoin/validation/verify.h" "evidence_verify_result<block_facts> verify")
require_text("src/bitcoin/validation/verify.h" "const block_validation_context& context")
require_text("src/bitcoin/validation/block.h" "witness_commitment")
forbid_text("src/bitcoin/validation/script.h" "virtual")
forbid_text("src/bitcoin/validation/block.h" "validation_context")
forbid_text("src/bitcoin/validation/transaction.h" "validation_context")
forbid_text("src/bitcoin/validation/transaction.h" "coin_index")
require_text("src/bitcoin/validation/verify.h" "block_validation_context")
require_text("src/bitcoin/validation/verify.h" "const block& candidate")
require_target_link_allowlist(
  "bitcoin_protocol"
  CMAKE_FILE "${SOURCE_DIR}/src/CMakeLists.txt"
  ALLOW_MISSING
)
require_target_link_allowlist(
  "bitcoin_chain_graph"
  CMAKE_FILE "${SOURCE_DIR}/src/CMakeLists.txt"
  ALLOWED "bitcoin_protocol"
)
require_target_link_allowlist(
  "bitcoin_validation_core_adapter"
  CMAKE_FILE "${SOURCE_DIR}/src/CMakeLists.txt"
  ALLOWED "bitcoin_validation" "bitcoin_chain_graph" "core_interface" "bitcoin_consensus"
)
forbid_text("src/CMakeLists.txt" "bitcoin_validation_library")
forbid_text("src/CMakeLists.txt" "add_library(bitcoin_node_ibd_validation")
forbid_text("src/CMakeLists.txt" "add_library(bitcoin_node_ibd_validation_core_adapters")
forbid_text("src/CMakeLists.txt" "bitcoin_node_ibd_validation")
forbid_text("src/CMakeLists.txt" "validation/validation_facade.cpp")
forbid_text("src/CMakeLists.txt" "validation/segment_validation_executor.cpp")
forbid_text("src/CMakeLists.txt" "validation/validation_pipeline.cpp")
forbid_text("src/test/CMakeLists.txt" "ibd_pipeline_tests.cpp")
forbid_text("src/bench/CMakeLists.txt" "ibd_pipeline.cpp")
forbid_text("src/test/CMakeLists.txt" "add_executable(test_validation_api_consumer")
require_text("src/test/CMakeLists.txt" "add_executable(test_validation_library_api_consumer")
require_text("src/test/CMakeLists.txt" "validation_library_api_consumer.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_validation_core_adapter_consumer")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_core_adapter_consumer.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_protocol_vocabulary")
require_text("src/test/CMakeLists.txt" "bitcoin_protocol_vocabulary_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_protocol_results")
require_text("src/test/CMakeLists.txt" "bitcoin_protocol_result_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_protocol_codecs")
require_text("src/test/CMakeLists.txt" "bitcoin_protocol_codec_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_validation_library_results")
require_text("src/test/CMakeLists.txt" "validation_library_result_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_validation_rule_inventory")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_rule_inventory_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_validation_header")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_header_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_validation_block")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_block_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_validation_script")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_script_tests.cpp")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_installed_package")
require_text("src/test/CMakeLists.txt" "TestBitcoinValidationInstalledPackage.cmake")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_validation_spend")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_spend_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_validation_transaction")
require_text("src/test/CMakeLists.txt" "bitcoin_validation_transaction_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_chain_view")
require_text("src/test/CMakeLists.txt" "bitcoin_chain_view_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_chain_graph")
require_text("src/test/CMakeLists.txt" "bitcoin_chain_graph_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_chain_graph_invariants")
require_text("src/test/CMakeLists.txt" "bitcoin_chain_graph_invariant_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_chain_graph_model")
require_text("src/test/CMakeLists.txt" "bitcoin_chain_graph_model_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_coin_index")
require_text("src/test/CMakeLists.txt" "bitcoin_coin_index_tests.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_std_bitcoin_style_validation_consumer")
require_text("src/test/CMakeLists.txt" "std_bitcoin_style_validation_consumer.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_libbitcoin_style_validation_consumer")
require_text("src/test/CMakeLists.txt" "libbitcoin_style_validation_consumer.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_hornet_style_validation_consumer")
require_text("src/test/CMakeLists.txt" "hornet_style_validation_consumer.cpp")
require_text("src/test/CMakeLists.txt" "add_executable(test_validation_library_header_self_containment")
require_text("src/test/CMakeLists.txt" "target_include_directories(test_validation_library_header_self_containment")
require_text("src/test/CMakeLists.txt" "add_executable(test_bitcoin_chain_graph_header_self_containment")
require_text("src/test/CMakeLists.txt" "api_manifest_boundary")
require_text("src/test/CMakeLists.txt" "validation_library_boundary")
require_text("src/test/validation_library_api_consumer.cpp" "#include <bitcoin/validation/api.h>")
forbid_text("src/test/validation_library_api_consumer.cpp" "#include <bitcoin/validation.h>")
require_target_link_allowlist(
  "bitcoin_validation"
  CMAKE_FILE "${SOURCE_DIR}/src/CMakeLists.txt"
  ALLOWED "bitcoin_protocol" "libsecp256k1::secp256k1"
)
require_target_link_allowlist(
  "bitcoin_validation_script_engine_objects"
  CMAKE_FILE "${SOURCE_DIR}/src/CMakeLists.txt"
  ALLOWED "libsecp256k1::secp256k1"
)
require_target_link_allowlist(
  "test_validation_library_api_consumer"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_bitcoin_validation_core_adapter_consumer"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation_core_adapter"
)
require_target_link_allowlist(
  "test_bitcoin_protocol_vocabulary"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_protocol"
)
require_target_link_allowlist(
  "test_bitcoin_protocol_results"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_protocol"
)
require_target_link_allowlist(
  "test_bitcoin_protocol_codecs"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_protocol"
)
require_target_link_allowlist(
  "test_validation_library_results"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_validation_rule_inventory"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_bitcoin_validation_header"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_bitcoin_validation_block"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_bitcoin_validation_script"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_bitcoin_validation_spend"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_bitcoin_validation_transaction"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_bitcoin_chain_view"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_protocol"
)
require_target_link_allowlist(
  "test_bitcoin_chain_graph"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_chain_graph"
)
require_target_link_allowlist(
  "test_bitcoin_chain_graph_invariants"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_chain_graph"
)
require_target_link_allowlist(
  "test_bitcoin_chain_graph_model"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_chain_graph"
)
require_target_link_allowlist(
  "test_bitcoin_coin_index"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_protocol"
)
require_target_link_allowlist(
  "test_std_bitcoin_style_validation_consumer"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_libbitcoin_style_validation_consumer"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)
require_target_link_allowlist(
  "test_hornet_style_validation_consumer"
  CMAKE_FILE "${SOURCE_DIR}/src/test/CMakeLists.txt"
  ALLOWED "bitcoin_validation"
)

file(GLOB_RECURSE validation_library_headers
  RELATIVE "${SOURCE_DIR}"
  "${SOURCE_DIR}/src/bitcoin/chain_graph/*.h"
  "${SOURCE_DIR}/src/bitcoin/protocol/*.h"
  "${SOURCE_DIR}/src/bitcoin/validation/*.h"
)
list(APPEND validation_library_headers "src/bitcoin/api.h")
file(GLOB_RECURSE validation_library_sources
  RELATIVE "${SOURCE_DIR}"
  "${SOURCE_DIR}/src/bitcoin/chain_graph/*.cpp"
  "${SOURCE_DIR}/src/bitcoin/protocol/*.cpp"
  "${SOURCE_DIR}/src/bitcoin/validation/*.cpp"
)

set(validation_library_files ${validation_library_headers} ${validation_library_sources})

set(validation_script_interpreter_bridge_allowed_text
  "#include <primitives/"
  "#include <script/"
  "CMutableTransaction"
  "COutPoint"
  "CScript"
  "CTransaction"
  "uint256"
)

foreach(relative_path IN LISTS validation_library_files)
  foreach(needle IN ITEMS
      "#include <chainstate.h>"
      "#include <chain.h>"
      "#include <coins.h>"
      "#include <consensus/"
      "#include <flatfile.h>"
      "#include <interfaces/"
      "#include <logging.h>"
      "#include <serialize.h>"
      "#include <sync.h>"
      "#include <util/"
      "#include <validation.h>"
      "#include <kernel/"
      "#include <node/"
      "#include <primitives/"
      "#include <script/"
      "#include <validation/"
      "#include <bitcoin/amount.h>"
      "#include <bitcoin/block.h>"
      "#include <bitcoin/chain.h>"
      "#include <bitcoin/codec.h>"
      "#include <bitcoin/hash.h>"
      "#include <bitcoin/result.h>"
      "#include <bitcoin/script.h>"
      "#include <bitcoin/transaction.h>"
      "#include <bitcoin/validation.h>"
      "ArgsManager"
      "BlockManager"
      "BlockValidationState"
      "CBlock"
      "CBlockHeader"
      "CBlockIndex"
      "CBlockUndo"
      "CChain"
      "CCoinsView"
      "CCoinsViewCache"
      "CMutableTransaction"
      "COutPoint"
      "CScript"
      "CTransaction"
      "CValidationState"
      "Chainstate"
      "ChainstateManager"
      "DataStream"
      "FlatFilePos"
      "LOCK("
      "Mutex"
      "NodeClock"
      "CurrentBlockValidationTime"
      "GetTime"
      "Now<"
      "READWRITE"
      "SERIALIZE_METHODS"
      "system_clock::now"
      "std::expected"
      "arith_uint256"
      "bool *"
      "bool*"
      "std::mutex"
      "throw "
      "cs_main"
      "gArgs"
      "uint256")
    if(relative_path STREQUAL "src/bitcoin/validation/script.cpp")
      list(FIND validation_script_interpreter_bridge_allowed_text "${needle}" allowed_bridge_text_index)
      if(NOT allowed_bridge_text_index EQUAL -1)
        continue()
      endif()
    endif()
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

foreach(relative_path IN ITEMS
    src/test/std_bitcoin_style_validation_consumer.cpp
    src/test/libbitcoin_style_validation_consumer.cpp
    src/test/hornet_style_validation_consumer.cpp)
  foreach(needle IN ITEMS
      "#include <chainstate.h>"
      "#include <chain.h>"
      "#include <coins.h>"
      "#include <consensus/"
      "#include <flatfile.h>"
      "#include <interfaces/"
      "#include <kernel/"
      "#include <node/"
      "#include <primitives/"
      "#include <serialize.h>"
      "#include <sync.h>"
      "#include <util/"
      "#include <validation.h>"
      "BlockValidationState"
      "CBlock"
      "CBlockIndex"
      "CBlockUndo"
      "CChain"
      "CCoinsView"
      "CCoinsViewCache"
      "COutPoint"
      "CScript"
      "CTransaction"
      "ChainstateManager"
      "FlatFilePos"
      "LOCK("
      "Mutex"
      "arith_uint256"
      "std::mutex"
      "std::shared_ptr"
      "uint256"
      "cs_main")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

foreach(needle IN ITEMS
    "#include <chainstate.h>"
    "#include <consensus/"
    "#include <interfaces/"
    "#include <kernel/"
    "#include <node/"
    "#include <primitives/"
    "#include <serialize.h>"
    "#include <util/"
    "#include <validation/"
    "BlockValidationState"
    "CBlock"
    "CBlockIndex"
    "CBlockUndo"
    "CCoinsViewCache"
    "CTransaction"
    "ChainstateManager"
    "FlatFilePos"
    "arith_uint256"
    "std::shared_ptr"
    "uint256"
    "cs_main")
  forbid_text("src/test/validation_library_api_consumer.cpp" "${needle}")
endforeach()
