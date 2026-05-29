# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

function(require_file relative_path)
  if(NOT EXISTS "${SOURCE_DIR}/${relative_path}")
    message(FATAL_ERROR "Expected ${relative_path} to exist")
  endif()
endfunction()

function(require_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} is missing required block-validation-boundary text: ${needle}")
  endif()
endfunction()

function(forbid_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(NOT match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} contains forbidden block-validation-boundary text: ${needle}")
  endif()
endfunction()

foreach(relative_path IN ITEMS
    src/validation/block_data_admission.cpp
    src/validation/block_data_admission.h
    src/validation/block_data_adapters.cpp
    src/validation/block_data_adapters.h
    src/validation/block_index_adapters.cpp
    src/validation/block_index_adapters.h
    src/validation/block_connection.cpp
    src/validation/block_connection.h
    src/validation/block_connection_trace.cpp
    src/validation/block_connection_trace.h
    src/validation/core_block_connection_context.cpp
    src/validation/core_block_connection_context.h
    src/validation/core_block_connection_setup.cpp
    src/validation/core_block_connection_setup.h
    src/validation/block_validation_internal.h
    src/validation/chain_validation.cpp
    src/validation/chain_validation.h)
  require_file("${relative_path}")
endforeach()

require_text("src/CMakeLists.txt" "block_data_adapters.cpp")
require_text("src/CMakeLists.txt" "block_data_admission.cpp")
require_text("src/CMakeLists.txt" "block_connection_trace.cpp")
require_text("src/CMakeLists.txt" "core_block_connection_context.cpp")
require_text("src/CMakeLists.txt" "core_block_connection_setup.cpp")
require_text("src/CMakeLists.txt" "add_library(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "BITCOIN_CHAIN_VALIDATION_SOURCES")
require_text("src/CMakeLists.txt" "target_link_libraries(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "bitcoin_chain_validation")
require_text("src/kernel/CMakeLists.txt" "bitcoin_chain_validation")
require_text("src/validation/block_validation.cpp" "#include <validation/block_validation_internal.h>")
require_text("src/validation/block_validation.cpp" "validation::BlockConnectionEngine")
require_text("src/validation/block_connection.h" "struct BlockConnectionContext")
require_text("src/validation/block_connection.h" "Consensus::BlockConsensusContext consensus_context")
require_text("src/validation/block_connection.h" "Consensus::BlockSpendConsensusOptions spend_options")
require_text("src/validation/block_connection.h" "struct BlockConnectionRuntime")
require_text("src/validation/block_connection.h" "kernel::Notifications& notifications")
require_text("src/validation/block_connection.h" "BlockDataStore& block_store")
require_text("src/validation/block_connection.h" "BlockIndexStore& block_index_store")
require_text("src/validation/block_connection.h" "Consensus::BlockScriptChecker& script_checker")
require_text("src/validation/block_connection.h" "BlockConnectionTrace& trace")
require_text("src/validation/block_connection_trace.h" "struct BlockConnectionTraceCounters")
require_text("src/validation/block_connection_trace.h" "BlockConnectionTraceCountersFor")
require_text("src/validation/block_connection_trace.h" "BlockConnectionTraceCounters m_counters")
require_text("src/validation/block_connection.h" "struct BlockConnectionRequest")
require_text("src/validation/block_connection.h" "class BlockConnectionEngine")
forbid_text("src/validation/block_connection.h" "Chainstate&")
forbid_text("src/validation/block_connection.h" "ChainstateManager&")
forbid_text("src/validation/block_connection.h" "BlockManager")
forbid_text("src/validation/block_connection.h" "CCheckQueue")
forbid_text("src/validation/block_connection.h" "CScriptCheck")
forbid_text("src/validation/block_connection.h" "ValidationCache")
forbid_text("src/validation/block_connection_trace.h" "ChainstateManager& m_chainman")
require_text("src/validation/chain_validation.cpp" "#include <validation/block_validation_internal.h>")
require_text("src/validation/chain_validation.h" "class ChainValidationService")
require_text("src/validation/block_validation_internal.h" "ProcessNewBlockHeaders(")
require_text("src/validation/block_validation_internal.h" "AcceptBlock(")
require_text("src/validation/block_validation_internal.h" "ProcessNewBlock(")
require_text("src/validation/block_validation_internal.h" "TestBlockValidity(")
require_text("src/validation/block_data_admission.h" "struct BlockDataAdmissionContext")
require_text("src/CMakeLists.txt" "block_index_adapters.cpp")
require_text("src/validation/block_data_adapters.h" "class BlockDataStore")
require_text("src/validation/block_data_adapters.h" "class CoreBlockDataStore")
require_text("src/validation/block_index_adapters.h" "class BlockIndexView")
require_text("src/validation/block_index_adapters.h" "class CoreBlockIndexView")
require_text("src/validation/block_index_adapters.h" "class BlockIndexStore")
require_text("src/validation/block_index_adapters.h" "class CoreBlockIndexStore")
require_text("src/validation/block_index_adapters.h" "MarkBlockIndexDirty")
require_text("src/validation/block_validation.cpp" "CoreBlockDataStore")
require_text("src/validation/block_validation.cpp" "CoreBlockIndexStore")
require_text("src/chainstate.cpp" "CoreBlockDataStore")
require_text("src/chainstate.cpp" "CoreBlockIndexStore")
require_text("src/validation/core_block_policy.cpp" "BlockIndexStore&")
require_text("src/validation/core_block_policy.h" "struct CoreBlockScriptCheckPolicy")
require_text("src/validation/core_block_policy.h" "const CoreBlockScriptCheckPolicy& policy")
forbid_text("src/validation/core_block_policy.h" "DetermineCoreBlockScriptChecks(ChainstateManager&")
forbid_text("src/validation/core_block_policy.cpp" "DetermineCoreBlockScriptChecks(ChainstateManager&")
require_text("src/validation/core_block_connection_context.h" "struct CoreBlockConnectionPolicySnapshot")
require_text("src/validation/core_block_connection_context.h" "SnapshotCoreBlockConnectionPolicy")
require_text("src/validation/core_block_connection_context.h" "PlanCoreBlockConnection")
require_text("src/validation/core_block_connection_context.h" "const CoreBlockConnectionPolicySnapshot& policy")
forbid_text("src/validation/core_block_connection_context.h" "PlanCoreBlockConnection(ChainstateManager&")
forbid_text("src/validation/core_block_connection_context.cpp" "PlanCoreBlockConnection(ChainstateManager&")
require_text("src/validation/core_block_connection_context.h" "MaybeLogCoreBlockConnectionScriptPolicy")
require_text("src/validation/core_block_connection_setup.h" "class CoreBlockConnectionSetup")
require_text("src/validation/core_block_connection_setup.h" "struct CoreBlockConnectionRuntimeInputs")
require_text("src/validation/core_block_connection_setup.h" "BlockDataStore& m_block_store")
require_text("src/validation/core_block_connection_setup.h" "BlockIndexStore& m_block_index_store")
require_text("src/validation/core_block_connection_setup.h" "CoreBlockScriptChecks m_script_checks")
require_text("src/validation/core_block_connection_setup.h" "BlockConnectionTrace m_trace")
forbid_text("src/validation/core_block_connection_setup.h" "Chainstate&")
forbid_text("src/validation/core_block_connection_setup.h" "ChainstateManager&")
forbid_text("src/validation/core_block_connection_setup.h" "CoreBlockDataStore m_block_store")
forbid_text("src/validation/core_block_connection_setup.h" "CoreBlockIndexStore m_block_index_store")
require_text("src/chainstate.cpp" "CoreBlockConnectionSetup")
require_text("src/kernel/bitcoinkernel.cpp" "CoreBlockDataStore")
require_text("src/kernel/bitcoinkernel.cpp" "CoreBlockIndexStore")

foreach(needle IN ITEMS
    "ProcessNewBlockHeaders("
    "AcceptBlock("
    "ProcessNewBlock("
    "TestBlockValidity(")
  forbid_text("src/validation/block_validation.h" "${needle}")
endforeach()

file(GLOB_RECURSE validation_boundary_sources
  "${SOURCE_DIR}/src/*.cpp"
  "${SOURCE_DIR}/src/*.h"
)
foreach(path IN LISTS validation_boundary_sources)
  file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${path}")
  if(NOT relative_path STREQUAL "src/validation/block_validation.cpp" AND
     NOT relative_path STREQUAL "src/validation/chain_validation.cpp")
    forbid_text("${relative_path}" "#include <validation/block_validation_internal.h>")
  endif()
endforeach()

foreach(relative_path IN ITEMS
    src/validation/block_validation.cpp
    src/chainstate.cpp
    src/kernel/bitcoinkernel.cpp)
  foreach(needle IN ITEMS
    "m_blockman.ReadBlock("
    "m_blockman.ReadBlockUndo("
    "m_blockman.WriteBlock("
    "m_blockman.UpdateBlockInfo("
    "m_blockman.AddToBlockIndex(")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

forbid_text("src/validation/block_validation.cpp" "m_blockman.m_block_index")
forbid_text("src/chainstate.cpp" "m_blockman.m_block_index")
forbid_text("src/chainstate.cpp" "m_blockman.LookupBlockIndex(")
forbid_text("src/chainstate.cpp" "m_dirty_blockindex")
forbid_text("src/validation/core_block_policy.cpp" "m_blockman.m_block_index")
forbid_text("src/kernel/bitcoinkernel.cpp" "m_blockman.LookupBlockIndex(")
