# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(AddWindowsResources)

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
if (NOT WITH_EXTERNAL_LIBMULTIPROCESS)
  include(../cmake/libmultiprocess.cmake)
  add_libmultiprocess(ipc/libmultiprocess)
endif()
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

include(${PROJECT_SOURCE_DIR}/cmake/BitcoinConsensusApi.cmake)

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
  consensus/script_view.cpp
  consensus/script_checker.cpp
  consensus/sigops.cpp
  consensus/snapshot_spend_state.cpp
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
  BITCOIN_CONSENSUS_INSTALL_HEADERS "${BITCOIN_CONSENSUS_INSTALL_HEADERS}"
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


# Core chain-validation sources shared by bitcoin_node and bitcoinkernel while
# the consensus boundary is being extracted.
set(BITCOIN_CHAIN_VALIDATION_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_data_admission.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_data_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_index_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_coin_effects.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_header_context_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_connection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_script_check_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_validation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_validation_error.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/block_validation_policy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/chain_validation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/coins_view_spend_state.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/connect_block_bench.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_commit_adapters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_connection_attempt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/core_block_policy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/validation/sequence_locks_adapters.cpp
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
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)
target_include_directories(bitcoin_chain_validation PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/include>)

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
  script/script_check.cpp
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
