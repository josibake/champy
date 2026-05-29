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
    src/block_data_admission.cpp
    src/block_data_admission.h
    src/block_data_adapters.cpp
    src/block_data_adapters.h
    src/block_index_adapters.cpp
    src/block_index_adapters.h)
  require_file("${relative_path}")
endforeach()

require_text("src/CMakeLists.txt" "block_data_adapters.cpp")
require_text("src/CMakeLists.txt" "block_data_admission.cpp")
require_text("src/block_data_admission.h" "struct BlockDataAdmissionContext")
require_text("src/CMakeLists.txt" "block_index_adapters.cpp")
require_text("src/block_data_adapters.h" "class BlockDataStore")
require_text("src/block_data_adapters.h" "class CoreBlockDataStore")
require_text("src/block_index_adapters.h" "class BlockIndexView")
require_text("src/block_index_adapters.h" "class CoreBlockIndexView")
require_text("src/block_index_adapters.h" "class BlockIndexStore")
require_text("src/block_index_adapters.h" "class CoreBlockIndexStore")
require_text("src/block_index_adapters.h" "MarkBlockIndexDirty")
require_text("src/block_validation.cpp" "CoreBlockDataStore")
require_text("src/block_validation.cpp" "CoreBlockIndexStore")
require_text("src/chainstate.cpp" "CoreBlockDataStore")
require_text("src/chainstate.cpp" "CoreBlockIndexStore")
require_text("src/core_block_policy.cpp" "CoreBlockIndexStore")
require_text("src/kernel/bitcoinkernel.cpp" "CoreBlockDataStore")
require_text("src/kernel/bitcoinkernel.cpp" "CoreBlockIndexStore")

foreach(relative_path IN ITEMS
    src/block_validation.cpp
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

forbid_text("src/block_validation.cpp" "m_blockman.m_block_index")
forbid_text("src/chainstate.cpp" "m_blockman.m_block_index")
forbid_text("src/chainstate.cpp" "m_blockman.LookupBlockIndex(")
forbid_text("src/chainstate.cpp" "m_dirty_blockindex")
forbid_text("src/core_block_policy.cpp" "m_blockman.m_block_index")
forbid_text("src/kernel/bitcoinkernel.cpp" "m_blockman.LookupBlockIndex(")
