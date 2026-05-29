# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

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

require_text("src/kernel/bitcoinkernel.h" "typedef struct btck_ChainstateManager")
require_text("src/kernel/bitcoinkernel.h" "btck_chainstate_manager_options_create")
require_text("src/kernel/bitcoinkernel.cpp" "#include <chain_validation.h>")

file(GLOB_RECURSE kernel_sources
  "${SOURCE_DIR}/src/kernel/*.cpp"
  "${SOURCE_DIR}/src/kernel/*.h"
)

foreach(path IN LISTS kernel_sources)
  file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${path}")
  foreach(needle IN ITEMS
      "#include <node/"
      "namespace node"
      "node::")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

forbid_text("src/kernel/CMakeLists.txt" "../node/")

foreach(needle IN ITEMS
    "CTxMemPool"
    "TxMemPool"
    "mempool"
    "Mempool"
    "node::"
    "#include <node/"
    "ChainstateMempoolSync")
  forbid_text("src/kernel/bitcoinkernel.h" "${needle}")
endforeach()
