# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(AddWindowsResources)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

configure_file(${PROJECT_SOURCE_DIR}/cmake/bitcoin-build-config.h.in bitcoin-build-config.h USE_SOURCE_PERMISSIONS @ONLY)
add_library(bitcoin_source_interface INTERFACE)
target_include_directories(bitcoin_source_interface INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(core_interface INTERFACE bitcoin_source_interface)

#=============================
# Subprojects
#=============================
# Subprojects include subdirectories that do or could have tests
# and/or benchmark binaries, such as all subtrees and univalue.
# These need to be included before CMAKE_*_OUTPUT_DIRECTORY variables
# are set, so output locations of subproject tests and libraries are
# not overridden.
include(../cmake/crc32c.cmake)
include(../cmake/leveldb.cmake)
include(../cmake/minisketch.cmake)
add_subdirectory(univalue)
include(../cmake/secp256k1.cmake)
find_secp256k1()

# Set top-level target output locations.
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()

set(IS_SOURCE_TARBALL FALSE)
# git will expand the next line to "set(IS_SOURCE_TARBALL TRUE)" inside archives:
#$Format:%nset(IS_SOURCE_TARBALL TRUE)$
if(NOT IS_SOURCE_TARBALL AND PROJECT_IS_TOP_LEVEL)
  find_package(Git QUIET)
endif()
add_custom_target(generate_build_info
  BYPRODUCTS ${PROJECT_BINARY_DIR}/src/bitcoin-build-info.h
  COMMAND ${CMAKE_COMMAND} -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -DBUILD_INFO_HEADER_PATH=${PROJECT_BINARY_DIR}/src/bitcoin-build-info.h -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/script/GenerateBuildInfo.cmake
  COMMENT "Generating bitcoin-build-info.h"
  VERBATIM
)
add_library(bitcoin_clientversion STATIC
  clientversion.cpp
)
target_link_libraries(bitcoin_clientversion
  PRIVATE
    core_interface
)
add_dependencies(bitcoin_clientversion generate_build_info)

add_subdirectory(crypto)
add_subdirectory(util)
add_subdirectory(ipc)
target_include_directories(bitcoin_crypto
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

include(${PROJECT_SOURCE_DIR}/cmake/BitcoinConsensusApi.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/BitcoinApiManifest.cmake)

add_library(bitcoin_protocol STATIC EXCLUDE_FROM_ALL
  bitcoin/protocol/codec.cpp
  crypto/sha256.cpp
  ${BITCOIN_PROTOCOL_PUBLIC_HEADERS}
)
target_compile_features(bitcoin_protocol
  PUBLIC
    cxx_std_20
)
set(bitcoin_protocol_private_sha256_definitions
  DISABLE_OPTIMIZED_SHA256
  CSHA256=BitcoinProtocolCSHA256
  SHA256AutoDetect=BitcoinProtocolSHA256AutoDetect
  SHA256D64=BitcoinProtocolSHA256D64
  sha256_implementation=bitcoin_protocol_sha256_implementation
)
target_compile_definitions(bitcoin_protocol
  PRIVATE
    ${bitcoin_protocol_private_sha256_definitions}
)
target_include_directories(bitcoin_protocol
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(bitcoin_chain_graph STATIC EXCLUDE_FROM_ALL
  bitcoin/chain_graph/chain_graph.cpp
  ${BITCOIN_CHAIN_GRAPH_PUBLIC_HEADERS}
)
target_compile_features(bitcoin_chain_graph
  PUBLIC
    cxx_std_20
)
target_include_directories(bitcoin_chain_graph
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(bitcoin_chain_graph
  PUBLIC
    bitcoin_protocol
)

add_library(bitcoin_validation STATIC EXCLUDE_FROM_ALL
  bitcoin/validation/block.cpp
  bitcoin/validation/header.cpp
  bitcoin/validation/script.cpp
  bitcoin/validation/spend.cpp
  bitcoin/validation/transaction.cpp
  bitcoin/validation/verify.cpp
  ${BITCOIN_VALIDATION_PUBLIC_HEADERS}
)
target_compile_features(bitcoin_validation
  PUBLIC
    cxx_std_20
)
target_include_directories(bitcoin_validation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
add_library(bitcoin_validation_script_engine_objects OBJECT
  crypto/hex_base.cpp
  crypto/hmac_sha512.cpp
  crypto/ripemd160.cpp
  crypto/sha1.cpp
  crypto/sha256.cpp
  crypto/sha512.cpp
  script/interpreter.cpp
  script/script.cpp
  script/script_error.cpp
  primitives/transaction.cpp
  pubkey.cpp
  hash.cpp
  uint256.cpp
)
target_compile_features(bitcoin_validation_script_engine_objects
  PRIVATE
    cxx_std_20
)
target_compile_definitions(bitcoin_validation_script_engine_objects
  PRIVATE
    DISABLE_OPTIMIZED_SHA256
    CSHA256=BitcoinValidationScriptEngineCSHA256
    SHA256AutoDetect=BitcoinValidationScriptEngineSHA256AutoDetect
    SHA256D64=BitcoinValidationScriptEngineSHA256D64
    sha256_implementation=bitcoin_validation_script_engine_sha256_implementation
)
target_include_directories(bitcoin_validation_script_engine_objects
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(bitcoin_validation_script_engine_objects
  PRIVATE
    libsecp256k1::secp256k1
)
target_sources(bitcoin_validation
  PRIVATE
    $<TARGET_OBJECTS:bitcoin_validation_script_engine_objects>
)

target_link_libraries(bitcoin_validation
  PUBLIC
    bitcoin_protocol
  PRIVATE
    libsecp256k1::secp256k1
)

add_library(bitcoin_kernel STATIC EXCLUDE_FROM_ALL
  bitcoin/kernel/chainstate.cpp
  bitcoin/kernel/context.cpp
  ${BITCOIN_KERNEL_PUBLIC_HEADERS}
)
# Build-tree-only draft: do not install or export bitcoin_kernel until the
# Core-backed chainstate operations are implemented by the C++ API instead of
# returning unsupported_operation.
target_compile_features(bitcoin_kernel
  PUBLIC
    cxx_std_20
)
target_include_directories(bitcoin_kernel
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(bitcoin_kernel
  PUBLIC
    bitcoin_protocol
)

install(TARGETS bitcoin_protocol bitcoin_validation
  EXPORT BitcoinValidationTargets
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT bitcoin_validation
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT bitcoin_validation
  ARCHIVE
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT bitcoin_validation
)

install(EXPORT BitcoinValidationTargets
  NAMESPACE Bitcoin::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BitcoinValidation"
  COMPONENT bitcoin_validation
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/BitcoinValidationConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/BitcoinValidationConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BitcoinValidation"
)
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/BitcoinValidationConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(FILES
    "${PROJECT_BINARY_DIR}/BitcoinValidationConfig.cmake"
    "${PROJECT_BINARY_DIR}/BitcoinValidationConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BitcoinValidation"
  COMPONENT bitcoin_validation
)

foreach(header IN LISTS BITCOIN_PROTOCOL_PUBLIC_HEADERS BITCOIN_VALIDATION_PUBLIC_HEADERS)
  get_filename_component(header_dir "${header}" DIRECTORY)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${header}"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${header_dir}"
    COMPONENT bitcoin_validation
  )
endforeach()

add_library(bitcoin_validation_core_adapter STATIC EXCLUDE_FROM_ALL
  bitcoin/core_adapter/api.h
  bitcoin/core_adapter/block.cpp
  bitcoin/core_adapter/block.h
  bitcoin/core_adapter/coin.cpp
  bitcoin/core_adapter/coin.h
  bitcoin/core_adapter/transaction.cpp
  bitcoin/core_adapter/transaction.h
)
target_compile_features(bitcoin_validation_core_adapter
  PUBLIC
    cxx_std_20
)
target_include_directories(bitcoin_validation_core_adapter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(bitcoin_validation_core_adapter
  PUBLIC
    bitcoin_validation
    bitcoin_chain_graph
  PRIVATE
    core_interface
    bitcoin_consensus
)

add_library(bitcoin_consensus STATIC EXCLUDE_FROM_ALL
  arith_uint256.cpp
  consensus/amount.cpp
  consensus/block_commit.cpp
  consensus/block_spend.cpp
  consensus/block_check.cpp
  consensus/block_facts.cpp
  consensus/coin_effects.cpp
  consensus/locktime.cpp
  consensus/merkle.cpp
  consensus/predicates.cpp
  consensus/pow.cpp
  consensus/sequence_locks.cpp
  consensus/segment_spend.cpp
  consensus/serialization.cpp
  consensus/script_view.cpp
  consensus/script_checker.cpp
  consensus/sigops.cpp
  consensus/snapshot_spend_state.cpp
  consensus/spend_state_batch.cpp
  consensus/tx_check.cpp
  consensus/block_consensus_pipeline.cpp
  hash.cpp
  primitives/block.cpp
  primitives/transaction.cpp
  pubkey.cpp
  script/interpreter.cpp
  script/script.cpp
  script/script_error.cpp
  uint256.cpp
)
target_sources(bitcoin_consensus
  PRIVATE
    ${BITCOIN_CONSENSUS_PUBLIC_API_HEADERS}
    ${BITCOIN_CONSENSUS_SUPPORT_HEADERS}
    ${BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS}
    ${BITCOIN_CONSENSUS_PUBLIC_TRANSITIVE_HEADERS}
    ${BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS}
)
target_compile_features(bitcoin_consensus
  PUBLIC
    cxx_std_20
)
target_include_directories(bitcoin_consensus
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# Keep this dependency list explicit. It is the build-level consensus extraction
# boundary and is checked by CheckConsensusBoundary.cmake.
target_link_libraries(bitcoin_consensus
  PRIVATE
    core_interface
    bitcoin_crypto
    secp256k1
)
set_target_properties(bitcoin_consensus PROPERTIES
  BITCOIN_CONSENSUS_PUBLIC_API_HEADERS "${BITCOIN_CONSENSUS_PUBLIC_API_HEADERS}"
  BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS "${BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS}"
  BITCOIN_CONSENSUS_PUBLIC_TRANSITIVE_HEADERS "${BITCOIN_CONSENSUS_PUBLIC_TRANSITIVE_HEADERS}"
  BITCOIN_CONSENSUS_SUPPORT_HEADERS "${BITCOIN_CONSENSUS_SUPPORT_HEADERS}"
  BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS "${BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS}"
)

# Home for common functionality shared by different executables and libraries.
# Similar to `bitcoin_util` library, but higher-level.
add_library(bitcoin_common STATIC EXCLUDE_FROM_ALL
  addresstype.cpp
  base58.cpp
  bech32.cpp
  chain.cpp
  chainparams.cpp
  chainparamsbase.cpp
  coins.cpp
  common/args.cpp
  common/bloom.cpp
  common/config.cpp
  common/init.cpp
  common/interfaces.cpp
  common/license_info.cpp
  common/messages.cpp
  common/netif.cpp
  common/pcp.cpp
  common/run_command.cpp
  common/settings.cpp
  common/signmessage.cpp
  common/system.cpp
  common/url.cpp
  compressor.cpp
  core_io.cpp
  deploymentinfo.cpp
  init/common.cpp
  kernel/chainparams.cpp
  key.cpp
  key_io.cpp
  merkleblock.cpp
  musig.cpp
  net_permissions.cpp
  net_types.cpp
  netaddress.cpp
  netbase.cpp
  outputtype.cpp
  policy/feerate.cpp
  policy/policy.cpp
  pow.cpp
  protocol.cpp
  scheduler.cpp
  script/descriptor.cpp
  script/miniscript.cpp
  script/parsing.cpp
  script/sign.cpp
  script/signingprovider.cpp
  script/solver.cpp
)
target_link_libraries(bitcoin_common
  PUBLIC
    univalue
  PRIVATE
    core_interface
    bitcoin_consensus
    bitcoin_util
    secp256k1
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

include(InstallBinaryComponent)


# Core's default storage and spend-state adapters. These are implementation
# choices for the Core runtime, not consensus requirements.
set(BITCOIN_CORE_STORAGE_ADAPTER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_data_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_index_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_coin_effects.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_replay.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/coins_view_spend_state.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_commit_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_coins_block_connection_state.cpp
)

add_library(bitcoin_core_storage_adapters STATIC EXCLUDE_FROM_ALL
  ${BITCOIN_CORE_STORAGE_ADAPTER_SOURCES}
)
target_link_libraries(bitcoin_core_storage_adapters
  PRIVATE
    core_interface
    bitcoin_consensus
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)
target_include_directories(bitcoin_core_storage_adapters PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/include>)

# Core chain-validation sources shared by bitcoin_node and bitcoinkernel while
# the consensus boundary is being extracted.
set(BITCOIN_CHAIN_VALIDATION_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_data_admission.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_header_context_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_connection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_script_check_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_validation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_validation_error.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_validation_policy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/chain_validation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_connection_trace.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_index_invariants.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_chain_activation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_chain_validation_runtimes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_connection_context.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_connection_snapshot.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_connection_setup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_connection_attempt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_policy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_check_queue_script_task_executor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/sequence_locks_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/script_task_executor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/runtime_time.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/script/script_check.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/snapshot_block_connection_state.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/validation_event_queue.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/tx_check_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/tx_verify.cpp
)

add_library(bitcoin_chain_validation STATIC EXCLUDE_FROM_ALL
  ${BITCOIN_CHAIN_VALIDATION_SOURCES}
)
target_link_libraries(bitcoin_chain_validation
  PRIVATE
    core_interface
    bitcoin_consensus
    bitcoin_core_storage_adapters
    bitcoin_util
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

# Local transaction admission, mempool state, and mempool-specific policy.
add_library(bitcoin_mempool_policy OBJECT EXCLUDE_FROM_ALL
  node/disconnected_transactions.cpp
  node/mempool_chain_sync.cpp
  node/mempool_removal_reason.cpp
  node/mempool_validation.cpp
  node/txmempool.cpp
  policy/ephemeral_policy.cpp
  policy/packages.cpp
  policy/rbf.cpp
  policy/truc_policy.cpp
  txgraph.cpp
)
target_link_libraries(bitcoin_mempool_policy
  PRIVATE
    core_interface
    bitcoin_common
    bitcoin_consensus
    bitcoin_util
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

# P2P and RPC server functionality used by `bitcoind` executable.
add_library(bitcoin_node STATIC EXCLUDE_FROM_ALL
  addrdb.cpp
  addrman.cpp
  banman.cpp
  bip324.cpp
  blockencodings.cpp
  dbwrapper.cpp
  deploymentstatus.cpp
  flatfile.cpp
  headerssync.cpp
  i2p.cpp
  init.cpp
  kernel/block_import_pipeline.cpp
  kernel/blk_file_scanner.cpp
  kernel/chain.cpp
  kernel/checks.cpp
  kernel/coinstats.cpp
  kernel/context.cpp
  kernel/cs_main.cpp
  kernel/blockimport.cpp
  kernel/blockstorage.cpp
  kernel/chainstate_load.cpp
  mapport.cpp
  net.cpp
  net_processing.cpp
  netgroup.cpp
  node/abort.cpp
  node/block_download_tracker.cpp
  node/block_inflight_index.cpp
  node/block_download_planner.cpp
  node/ibd_block_processor.cpp
  node/blockmanager_args.cpp
  node/caches.cpp
  node/chainstatemanager_args.cpp
  node/coin.cpp
  node/coins_view_args.cpp
  node/connection_types.cpp
  node/context.cpp
  node/database_args.cpp
  node/eviction.cpp
  node/interface_ui.cpp
  node/interfaces.cpp
  node/kernel_notifications.cpp
  node/mempool_args.cpp
  node/mempool_persist.cpp
  node/mempool_persist_args.cpp
  node/miner.cpp
  node/mini_miner.cpp
  node/minisketchwrapper.cpp
  node/peerman_args.cpp
  node/timeoffsets.cpp
  node/transaction.cpp
  node/txdownloadman_impl.cpp
  node/txorphanage.cpp
  node/txreconciliation.cpp
  node/warnings.cpp
  noui.cpp
  policy/fees/block_policy_estimator.cpp
  policy/fees/block_policy_estimator_args.cpp
  policy/settings.cpp
  private_broadcast.cpp
  script/sigcache.cpp
  signet.cpp
  torcontrol.cpp
  txdb.cpp
  txrequest.cpp
  chainstate.cpp
  validationinterface.cpp
  versionbits.cpp
  $<TARGET_OBJECTS:bitcoin_mempool_policy>
)
target_link_libraries(bitcoin_node
  PUBLIC
    univalue
  PRIVATE
    core_interface
    bitcoin_chain_validation
    bitcoin_common
    bitcoin_util
    leveldb
    minisketch
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)
if(WITH_EMBEDDED_ASMAP)
  target_compile_definitions(bitcoin_node PRIVATE ENABLE_EMBEDDED_ASMAP=1)
  include(TargetDataSources)
  target_raw_data_sources(bitcoin_node NAMESPACE node::data
    node/data/ip_asn.dat
  )
endif()

# Bitcoin wrapper executable that can call other executables.
if(BUILD_BITCOIN_BIN)
  add_executable(bitcoin bitcoin.cpp)
  add_windows_resources(bitcoin bitcoin-res.rc)
  add_windows_application_manifest(bitcoin)
  target_link_libraries(bitcoin core_interface bitcoin_common bitcoin_util)
  install_binary_component(bitcoin)
endif()

# Bitcoin Core bitcoind.
if(BUILD_DAEMON)
  add_executable(bitcoind
    bitcoind.cpp
    init/bitcoind.cpp
  )
  add_windows_resources(bitcoind bitcoind-res.rc)
  add_windows_application_manifest(bitcoind)
  target_link_libraries(bitcoind
    core_interface
    bitcoin_node
    bitcoin_ipc
  )
  install_binary_component(bitcoind)
endif()
if(BUILD_DAEMON)
  add_executable(bitcoin-node
    bitcoind.cpp
    init/bitcoin-node.cpp
  )
  target_link_libraries(bitcoin-node
    core_interface
    bitcoin_node
    bitcoin_ipc
  )
  install_binary_component(bitcoin-node INTERNAL)
endif()


add_library(bitcoin_cli STATIC EXCLUDE_FROM_ALL
  compat/stdin.cpp
)
target_link_libraries(bitcoin_cli
  PUBLIC
    core_interface
)


# Bitcoin Core RPC client
if(BUILD_CLI)
  add_executable(bitcoin-cli bitcoin-cli.cpp init/basic.cpp)
  add_windows_resources(bitcoin-cli bitcoin-cli-res.rc)
  add_windows_application_manifest(bitcoin-cli)
  target_link_libraries(bitcoin-cli
    core_interface
    bitcoin_cli
    bitcoin_common
    bitcoin_ipc
    bitcoin_util
  )
  install_binary_component(bitcoin-cli)
endif()


if(BUILD_KERNEL_LIB)
  add_subdirectory(kernel)
  if (BUILD_KERNEL_TEST)
    add_subdirectory(test/kernel)
  endif()
  if(BUILD_UTIL_CHAINSTATE)
    add_executable(bitcoin-chainstate
      bitcoin-chainstate.cpp
    )
    add_windows_application_manifest(bitcoin-chainstate)
    target_link_libraries(bitcoin-chainstate
      PRIVATE
        core_interface
        bitcoinkernel
    )
    install_binary_component(bitcoin-chainstate INTERNAL)
  endif()
endif()

set_property(TARGET bitcoin_crypto PROPERTY LINK_LIBRARIES "")
set_property(TARGET bitcoin_crypto PROPERTY INTERFACE_LINK_LIBRARIES "")

add_subdirectory(test/util)
if(BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

if(BUILD_FUZZ_BINARY)
  add_subdirectory(test/fuzz)
endif()
