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
    src/validation/block_validation_internal.h
    src/validation/chain_validation.cpp
    src/validation/chain_validation.h)
  require_file("${relative_path}")
endforeach()

require_text("src/CMakeLists.txt" "block_data_adapters.cpp")
require_text("src/CMakeLists.txt" "block_data_admission.cpp")
require_text("src/CMakeLists.txt" "add_library(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "BITCOIN_CHAIN_VALIDATION_SOURCES")
require_text("src/CMakeLists.txt" "target_link_libraries(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "bitcoin_chain_validation")
require_text("src/kernel/CMakeLists.txt" "bitcoin_chain_validation")
require_text("src/validation/block_validation.cpp" "#include <validation/block_validation_internal.h>")
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
require_text("src/validation/core_block_policy.cpp" "CoreBlockIndexStore")
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
