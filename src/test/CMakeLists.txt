# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(${PROJECT_SOURCE_DIR}/cmake/BitcoinApiManifest.cmake)

# Do not use generator expressions in test sources because the
# SOURCES property is processed to gather test suite macros.
add_executable(test_bitcoin
  main.cpp
  addrman_tests.cpp
  allocator_tests.cpp
  amount_tests.cpp
  argsman_tests.cpp
  arith_uint256_tests.cpp
  banman_tests.cpp
  base32_tests.cpp
  base58_tests.cpp
  base64_tests.cpp
  bech32_tests.cpp
  bip32_tests.cpp
  bip324_tests.cpp
  blk_file_scanner_tests.cpp
  blockchain_tests.cpp
  block_download_tracker_tests.cpp
  block_inflight_index_tests.cpp
  block_download_planner_tests.cpp
  block_index_invariant_tests.cpp
  block_validation_adapters_tests.cpp
  blockencodings_tests.cpp
  blockmanager_tests.cpp
  block_data_admission_tests.cpp
  bloom_tests.cpp
  bswap_tests.cpp
  btcsignals_tests.cpp
  caches_tests.cpp
  chain_tests.cpp
  chain_validation_tests.cpp
  chainstate_write_tests.cpp
  checkqueue_tests.cpp
  cluster_linearize_tests.cpp
  coins_tests.cpp
  coinscachepair_tests.cpp
  coinsviewoverlay_tests.cpp
  consensus_block_check_tests.cpp
  consensus_block_commit_tests.cpp
  consensus_block_facts_tests.cpp
  consensus_block_spend_tests.cpp
  consensus_amount_tests.cpp
  consensus_conformance_tests.cpp
  consensus_locktime_tests.cpp
  consensus_predicates_tests.cpp
  consensus_script_checker_tests.cpp
  consensus_serialization_tests.cpp
  consensus_segment_spend_tests.cpp
  consensus_snapshot_spend_state_tests.cpp
  consensus_spend_state_batch_tests.cpp
  consensus_tx_verify_tests.cpp
  consensus_block_consensus_pipeline_tests.cpp
  core_block_policy_tests.cpp
  common_url_tests.cpp
  compress_tests.cpp
  crypto_tests.cpp
  cuckoocache_tests.cpp
  dbwrapper_tests.cpp
  denialofservice_tests.cpp
  descriptor_tests.cpp
  disconnected_transactions.cpp
  feefrac_tests.cpp
  feerounder_tests.cpp
  flatfile_tests.cpp
  fs_tests.cpp
  getarg_tests.cpp
  hash_tests.cpp
  headers_sync_chainwork_tests.cpp
  i2p_tests.cpp
  interfaces_tests.cpp
  key_io_tests.cpp
  key_tests.cpp
  logging_tests.cpp
  mempool_tests.cpp
  merkle_tests.cpp
  merkleblock_tests.cpp
  miner_tests.cpp
  miniminer_tests.cpp
  miniscript_tests.cpp
  minisketch_tests.cpp
  mock_process.cpp
  multisig_tests.cpp
  bip328_tests.cpp
  net_peer_connection_tests.cpp
  net_peer_eviction_tests.cpp
  net_tests.cpp
  netbase_tests.cpp
  node_init_tests.cpp
  node_warnings_tests.cpp
  orphanage_tests.cpp
  pcp_tests.cpp
  peerman_tests.cpp
  pmt_tests.cpp
  policyestimator_tests.cpp
  pool_tests.cpp
  pow_tests.cpp
  prevector_tests.cpp
  private_broadcast_tests.cpp
  random_tests.cpp
  rbf_tests.cpp
  result_tests.cpp
  reverselock_tests.cpp
  sanity_tests.cpp
  scheduler_tests.cpp
  script_assets_tests.cpp
  script_p2sh_tests.cpp
  script_parse_tests.cpp
  script_segwit_tests.cpp
  script_standard_tests.cpp
  script_tests.cpp
  scriptnum_tests.cpp
  serfloat_tests.cpp
  serialize_tests.cpp
  settings_tests.cpp
  sighash_tests.cpp
  sigopcount_tests.cpp
  skiplist_tests.cpp
  sock_tests.cpp
  span_tests.cpp
  streams_tests.cpp
  sync_tests.cpp
  system_ram_tests.cpp
  system_tests.cpp
  testnet4_miner_tests.cpp
  threadpool_tests.cpp
  timeoffsets_tests.cpp
  torcontrol_tests.cpp
  transaction_tests.cpp
  translation_tests.cpp
  txdownload_tests.cpp
  txgraph_tests.cpp
  txpackage_tests.cpp
  txreconciliation_tests.cpp
  txrequest_tests.cpp
  txvalidation_tests.cpp
  txvalidationcache_tests.cpp
  uint256_tests.cpp
  util_check_tests.cpp
  util_expected_tests.cpp
  util_string_tests.cpp
  util_tests.cpp
  util_threadnames_tests.cpp
  util_trace_tests.cpp
  validation_block_tests.cpp
  validation_chainstate_tests.cpp
  validation_chainstatemanager_tests.cpp
  validation_flush_tests.cpp
  validation_tests.cpp
  validationinterface_tests.cpp
  versionbits_tests.cpp
)

include(TargetDataSources)
target_json_data_sources(test_bitcoin
  data/base58_encode_decode.json
  data/bip341_wallet_vectors.json
  data/consensus_conformance.json
  data/key_io_invalid.json
  data/key_io_valid.json
  data/script_tests.json
  data/sighash.json
  data/tx_invalid.json
  data/tx_valid.json
)
target_raw_data_sources(test_bitcoin NAMESPACE test::data
  data/asmap.raw
)

add_windows_application_manifest(test_bitcoin)

target_link_libraries(test_bitcoin
  core_interface
  test_util
  bitcoin_cli
  bitcoin_node
  bitcoin_consensus
  minisketch
  secp256k1
  Boost::headers
  $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

add_executable(test_consensus_library_boundary
  consensus_library_boundary.cpp
)
target_link_libraries(test_consensus_library_boundary
  PRIVATE
    core_interface
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,bitcoin_consensus>"
)
add_test(NAME consensus_library_boundary COMMAND test_consensus_library_boundary)

# This target is the internal-consumer boundary proof. Keep it small: it should
# link the consensus implementation library and the minimum compile/test support
# needed by in-tree callers.
add_executable(test_consensus_internal_consumer
  consensus_internal_consumer_tests.cpp
)
target_link_libraries(test_consensus_internal_consumer
  PRIVATE
    bitcoin_consensus
    Boost::headers
)
add_test(NAME consensus_internal_consumer COMMAND test_consensus_internal_consumer)

add_executable(test_validation_library_api_consumer
  validation_library_api_consumer.cpp
)
target_link_libraries(test_validation_library_api_consumer
  PRIVATE
    bitcoin_validation
)
add_test(NAME validation_library_api_consumer COMMAND test_validation_library_api_consumer)

add_executable(test_bitcoin_validation_core_adapter_consumer
  bitcoin_validation_core_adapter_consumer.cpp
)
target_link_libraries(test_bitcoin_validation_core_adapter_consumer
  PRIVATE
    bitcoin_validation_core_adapter
)
add_test(NAME bitcoin_validation_core_adapter_consumer COMMAND test_bitcoin_validation_core_adapter_consumer)

add_executable(test_bitcoin_protocol_vocabulary
  bitcoin_protocol_vocabulary_tests.cpp
)
target_link_libraries(test_bitcoin_protocol_vocabulary
  PRIVATE
    bitcoin_protocol
)
add_test(NAME bitcoin_protocol_vocabulary COMMAND test_bitcoin_protocol_vocabulary)

add_executable(test_bitcoin_protocol_results
  bitcoin_protocol_result_tests.cpp
)
target_link_libraries(test_bitcoin_protocol_results
  PRIVATE
    bitcoin_protocol
)
add_test(NAME bitcoin_protocol_results COMMAND test_bitcoin_protocol_results)

add_executable(test_bitcoin_protocol_codecs
  bitcoin_protocol_codec_tests.cpp
)
target_link_libraries(test_bitcoin_protocol_codecs
  PRIVATE
    bitcoin_protocol
)
add_test(NAME bitcoin_protocol_codecs COMMAND test_bitcoin_protocol_codecs)

add_executable(test_bitcoin_chain_view
  bitcoin_chain_view_tests.cpp
)
target_link_libraries(test_bitcoin_chain_view
  PRIVATE
    bitcoin_protocol
)
add_test(NAME bitcoin_chain_view COMMAND test_bitcoin_chain_view)

add_executable(test_bitcoin_chain_graph
  bitcoin_chain_graph_tests.cpp
)
target_link_libraries(test_bitcoin_chain_graph
  PRIVATE
    bitcoin_chain_graph
)
add_test(NAME bitcoin_chain_graph COMMAND test_bitcoin_chain_graph)

add_executable(test_bitcoin_chain_graph_invariants
  bitcoin_chain_graph_invariant_tests.cpp
)
target_link_libraries(test_bitcoin_chain_graph_invariants
  PRIVATE
    bitcoin_chain_graph
)
add_test(NAME bitcoin_chain_graph_invariants COMMAND test_bitcoin_chain_graph_invariants)

add_executable(test_bitcoin_chain_graph_model
  bitcoin_chain_graph_model_tests.cpp
)
target_link_libraries(test_bitcoin_chain_graph_model
  PRIVATE
    bitcoin_chain_graph
)
add_test(NAME bitcoin_chain_graph_model COMMAND test_bitcoin_chain_graph_model)

add_executable(test_bitcoin_coin_index
  bitcoin_coin_index_tests.cpp
)
target_link_libraries(test_bitcoin_coin_index
  PRIVATE
    bitcoin_protocol
)
add_test(NAME bitcoin_coin_index COMMAND test_bitcoin_coin_index)

add_executable(test_std_bitcoin_style_validation_consumer
  std_bitcoin_style_validation_consumer.cpp
)
target_link_libraries(test_std_bitcoin_style_validation_consumer
  PRIVATE
    bitcoin_validation
)
add_test(NAME std_bitcoin_style_validation_consumer COMMAND test_std_bitcoin_style_validation_consumer)

add_executable(test_libbitcoin_style_validation_consumer
  libbitcoin_style_validation_consumer.cpp
)
target_link_libraries(test_libbitcoin_style_validation_consumer
  PRIVATE
    bitcoin_validation
)
add_test(NAME libbitcoin_style_validation_consumer COMMAND test_libbitcoin_style_validation_consumer)

add_executable(test_hornet_style_validation_consumer
  hornet_style_validation_consumer.cpp
)
target_link_libraries(test_hornet_style_validation_consumer
  PRIVATE
    bitcoin_validation
)
add_test(NAME hornet_style_validation_consumer COMMAND test_hornet_style_validation_consumer)

add_executable(test_validation_library_results
  validation_library_result_tests.cpp
)
target_link_libraries(test_validation_library_results
  PRIVATE
    bitcoin_validation
)
add_test(NAME validation_library_results COMMAND test_validation_library_results)

add_executable(test_validation_rule_inventory
  bitcoin_validation_rule_inventory_tests.cpp
)
target_link_libraries(test_validation_rule_inventory
  PRIVATE
    bitcoin_validation
)
add_test(NAME validation_rule_inventory COMMAND test_validation_rule_inventory)

add_executable(test_bitcoin_validation_header
  bitcoin_validation_header_tests.cpp
)
target_link_libraries(test_bitcoin_validation_header
  PRIVATE
    bitcoin_validation
)
add_test(NAME bitcoin_validation_header COMMAND test_bitcoin_validation_header)

add_executable(test_bitcoin_validation_block
  bitcoin_validation_block_tests.cpp
)
target_link_libraries(test_bitcoin_validation_block
  PRIVATE
    bitcoin_validation
)
add_test(NAME bitcoin_validation_block COMMAND test_bitcoin_validation_block)

add_executable(test_bitcoin_validation_transaction
  bitcoin_validation_transaction_tests.cpp
)
target_link_libraries(test_bitcoin_validation_transaction
  PRIVATE
    bitcoin_validation
)
add_test(NAME bitcoin_validation_transaction COMMAND test_bitcoin_validation_transaction)

add_executable(test_bitcoin_validation_spend
  bitcoin_validation_spend_tests.cpp
)
target_link_libraries(test_bitcoin_validation_spend
  PRIVATE
    bitcoin_validation
)
add_test(NAME bitcoin_validation_spend COMMAND test_bitcoin_validation_spend)

add_executable(test_bitcoin_validation_script
  bitcoin_validation_script_tests.cpp
)
target_link_libraries(test_bitcoin_validation_script
  PRIVATE
    bitcoin_validation
)
add_test(NAME bitcoin_validation_script COMMAND test_bitcoin_validation_script)

add_test(
  NAME bitcoin_validation_installed_package
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DBUILD_DIR=${PROJECT_BINARY_DIR}
    -Dlibsecp256k1_DIR=${libsecp256k1_DIR}
    -DTEST_CONFIGURATION=$<CONFIG>
    -P ${PROJECT_SOURCE_DIR}/cmake/script/TestBitcoinValidationInstalledPackage.cmake
)

set(validation_library_header_self_containment_sources)
set(validation_library_header_self_containment_dir "${CMAKE_CURRENT_BINARY_DIR}/validation_library_header_self_containment")
set(validation_library_install_shape_include_dir "${validation_library_header_self_containment_dir}/include")
file(MAKE_DIRECTORY "${validation_library_header_self_containment_dir}")
file(MAKE_DIRECTORY "${validation_library_install_shape_include_dir}")
foreach(relative_header IN LISTS BITCOIN_VALIDATION_INSTALL_SHAPE_HEADERS)
  configure_file(
    "${PROJECT_SOURCE_DIR}/src/${relative_header}"
    "${validation_library_install_shape_include_dir}/${relative_header}"
    COPYONLY
  )
  set(symbol "${relative_header}")
  string(REPLACE "/" "_" symbol "${symbol}")
  string(REPLACE "." "_" symbol "${symbol}")
  set(generated_source "${validation_library_header_self_containment_dir}/${symbol}.cpp")
  file(WRITE "${generated_source}"
"// Generated by src/test/CMakeLists.txt
#include <${relative_header}>
void validation_library_header_self_containment_${symbol}() {}
")
  list(APPEND validation_library_header_self_containment_sources "${generated_source}")
endforeach()
set(validation_library_header_self_containment_main "${validation_library_header_self_containment_dir}/main.cpp")
file(WRITE "${validation_library_header_self_containment_main}"
"// Generated by src/test/CMakeLists.txt
int main() { return 0; }
")
add_executable(test_validation_library_header_self_containment
  ${validation_library_header_self_containment_sources}
  ${validation_library_header_self_containment_main}
)
target_compile_features(test_validation_library_header_self_containment
  PRIVATE
    cxx_std_20
)
target_include_directories(test_validation_library_header_self_containment
  PRIVATE
    "${validation_library_install_shape_include_dir}"
)
add_test(NAME validation_library_header_self_containment COMMAND test_validation_library_header_self_containment)

set(chain_graph_header_self_containment_sources)
set(chain_graph_header_self_containment_dir "${CMAKE_CURRENT_BINARY_DIR}/chain_graph_header_self_containment")
set(chain_graph_install_shape_include_dir "${chain_graph_header_self_containment_dir}/include")
file(MAKE_DIRECTORY "${chain_graph_header_self_containment_dir}")
file(MAKE_DIRECTORY "${chain_graph_install_shape_include_dir}")
foreach(relative_header IN LISTS BITCOIN_CHAIN_GRAPH_INSTALL_SHAPE_HEADERS)
  configure_file(
    "${PROJECT_SOURCE_DIR}/src/${relative_header}"
    "${chain_graph_install_shape_include_dir}/${relative_header}"
    COPYONLY
  )
  set(symbol "${relative_header}")
  string(REPLACE "/" "_" symbol "${symbol}")
  string(REPLACE "." "_" symbol "${symbol}")
  set(generated_source "${chain_graph_header_self_containment_dir}/${symbol}.cpp")
  file(WRITE "${generated_source}"
"// Generated by src/test/CMakeLists.txt
#include <${relative_header}>
void chain_graph_header_self_containment_${symbol}() {}
")
  list(APPEND chain_graph_header_self_containment_sources "${generated_source}")
endforeach()
set(chain_graph_header_self_containment_main "${chain_graph_header_self_containment_dir}/main.cpp")
file(WRITE "${chain_graph_header_self_containment_main}"
"// Generated by src/test/CMakeLists.txt
int main() { return 0; }
")
add_executable(test_bitcoin_chain_graph_header_self_containment
  ${chain_graph_header_self_containment_sources}
  ${chain_graph_header_self_containment_main}
)
target_compile_features(test_bitcoin_chain_graph_header_self_containment
  PRIVATE
    cxx_std_20
)
target_include_directories(test_bitcoin_chain_graph_header_self_containment
  PRIVATE
    "${chain_graph_install_shape_include_dir}"
)
add_test(NAME bitcoin_chain_graph_header_self_containment COMMAND test_bitcoin_chain_graph_header_self_containment)

add_test(
  NAME consensus_source_boundary
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/script/CheckConsensusBoundary.cmake
)

add_test(
  NAME block_validation_boundary
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/script/CheckBlockValidationBoundary.cmake
)

add_test(
  NAME api_manifest_boundary
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/script/CheckApiManifest.cmake
)

add_test(
  NAME validation_library_boundary
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/script/CheckValidationLibraryBoundary.cmake
)

add_test(
  NAME mempool_boundary
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/script/CheckMempoolBoundary.cmake
)

add_test(
  NAME kernel_boundary
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/script/CheckKernelBoundary.cmake
)

file(GLOB consensus_public_headers CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/consensus/*.h"
)
set(consensus_header_self_containment_sources)
set(consensus_header_self_containment_dir "${CMAKE_CURRENT_BINARY_DIR}/consensus_header_self_containment")
file(MAKE_DIRECTORY "${consensus_header_self_containment_dir}")
foreach(header_path IN LISTS consensus_public_headers)
  file(RELATIVE_PATH relative_header "${PROJECT_SOURCE_DIR}/src" "${header_path}")
  set(symbol "${relative_header}")
  string(REPLACE "/" "_" symbol "${symbol}")
  string(REPLACE "." "_" symbol "${symbol}")
  set(generated_source "${consensus_header_self_containment_dir}/${symbol}.cpp")
  file(WRITE "${generated_source}"
"// Generated by src/test/CMakeLists.txt
#include <${relative_header}>
void consensus_header_self_containment_${symbol}() {}
")
  list(APPEND consensus_header_self_containment_sources "${generated_source}")
endforeach()
set(consensus_header_self_containment_main "${consensus_header_self_containment_dir}/main.cpp")
file(WRITE "${consensus_header_self_containment_main}"
"// Generated by src/test/CMakeLists.txt
int main() { return 0; }
")
add_executable(test_consensus_header_self_containment
  ${consensus_header_self_containment_sources}
  ${consensus_header_self_containment_main}
)
target_link_libraries(test_consensus_header_self_containment
  PRIVATE
    core_interface
)
add_test(NAME consensus_header_self_containment COMMAND test_consensus_header_self_containment)

add_subdirectory(${PROJECT_SOURCE_DIR}/src/ipc/test ipc)

function(add_boost_test source_file)
  if(NOT EXISTS ${source_file})
    return()
  endif()

  file(READ "${source_file}" source_file_content)
  string(REGEX
    MATCHALL "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(([A-Za-z0-9_]+)"
    test_suite_macro "${source_file_content}"
  )
  list(TRANSFORM test_suite_macro
    REPLACE "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(" ""
  )
  # The mock_process test suite does not contain unit tests.
  list(REMOVE_ITEM test_suite_macro "mock_process")
  foreach(test_suite_name IN LISTS test_suite_macro)
    add_test(NAME ${test_suite_name}
      COMMAND test_bitcoin --run_test=${test_suite_name} --catch_system_error=no --log_level=test_suite -- -printtoconsole=1
    )
    set_property(TEST ${test_suite_name} PROPERTY
      SKIP_REGULAR_EXPRESSION
        "no test cases matching filter"
        "skipping script_assets_test"
        "skipping total_ram"
    )
  endforeach()
endfunction()

function(add_all_test_targets)
  get_target_property(test_source_dir test_bitcoin SOURCE_DIR)
  get_target_property(test_sources test_bitcoin SOURCES)
  foreach(test_source ${test_sources})
    cmake_path(IS_RELATIVE test_source result)
    if(result)
      cmake_path(APPEND test_source_dir ${test_source} OUTPUT_VARIABLE test_source)
    endif()
    add_boost_test(${test_source})
  endforeach()
endfunction()

add_all_test_targets()

install_binary_component(test_bitcoin INTERNAL)
