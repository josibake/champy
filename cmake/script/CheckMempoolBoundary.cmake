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

function(forbid_file relative_path)
  if(EXISTS "${SOURCE_DIR}/${relative_path}")
    message(FATAL_ERROR "Unexpected mempool file outside node boundary: ${relative_path}")
  endif()
endfunction()

function(forbid_glob pattern)
  file(GLOB matches RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/${pattern}")
  if(matches)
    message(FATAL_ERROR "Unexpected mempool files outside node boundary: ${matches}")
  endif()
endfunction()

function(forbid_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(NOT match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} contains forbidden mempool-boundary text: ${needle}")
  endif()
endfunction()

function(require_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} is missing required mempool-boundary text: ${needle}")
  endif()
endfunction()

function(extract_cmake_call_from_file out_var cmake_file call_name target_name)
  file(READ "${cmake_file}" cmake_contents)
  string(FIND "${cmake_contents}" "${call_name}(${target_name}" call_start)
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

foreach(relative_path IN ITEMS
    src/chainstate_event_sink.h
    src/node/disconnected_transactions.cpp
    src/node/disconnected_transactions.h
    src/node/mempool_chain_sync.cpp
    src/node/mempool_chain_sync.h
    src/node/mempool_entry.h
    src/node/mempool_limits.h
    src/node/mempool_options.h
    src/node/mempool_removal_reason.cpp
    src/node/mempool_removal_reason.h
    src/node/mempool_validation.cpp
    src/node/mempool_validation.h
    src/node/mempool_validation_result.h
    src/node/txmempool.cpp
    src/node/txmempool.h)
  require_file("${relative_path}")
endforeach()

set(expected_mempool_policy_sources
  "node/disconnected_transactions.cpp"
  "node/mempool_chain_sync.cpp"
  "node/mempool_removal_reason.cpp"
  "node/mempool_validation.cpp"
  "node/txmempool.cpp"
  "policy/ephemeral_policy.cpp"
  "policy/packages.cpp"
  "policy/rbf.cpp"
  "policy/truc_policy.cpp"
  "txgraph.cpp"
)

extract_cmake_call_from_file(mempool_policy_sources_block "${SOURCE_DIR}/src/CMakeLists.txt" "add_library" "bitcoin_mempool_policy")
if(NOT mempool_policy_sources_block MATCHES "OBJECT")
  message(FATAL_ERROR "bitcoin_mempool_policy must be an explicit object target")
endif()
foreach(source IN LISTS expected_mempool_policy_sources)
  string(FIND "${mempool_policy_sources_block}" "${source}" source_index)
  if(source_index EQUAL -1)
    message(FATAL_ERROR "bitcoin_mempool_policy is missing ${source}")
  endif()
endforeach()

extract_cmake_call_from_file(bitcoin_node_sources_block "${SOURCE_DIR}/src/CMakeLists.txt" "add_library" "bitcoin_node")
string(FIND "${bitcoin_node_sources_block}" "$<TARGET_OBJECTS:bitcoin_mempool_policy>" mempool_objects_index)
if(mempool_objects_index EQUAL -1)
  message(FATAL_ERROR "bitcoin_node must include bitcoin_mempool_policy objects")
endif()
foreach(source IN LISTS expected_mempool_policy_sources)
  string(FIND "${bitcoin_node_sources_block}" "${source}" source_index)
  if(NOT source_index EQUAL -1)
    message(FATAL_ERROR "bitcoin_node lists mempool admission source directly instead of using bitcoin_mempool_policy: ${source}")
  endif()
endforeach()

foreach(relative_path IN ITEMS
    src/chainstate_mempool_sync.h
    src/kernel/disconnected_transactions.cpp
    src/kernel/disconnected_transactions.h
    src/mempool_validation.cpp
    src/mempool_validation.h
    src/txmempool.cpp
    src/txmempool.h)
  forbid_file("${relative_path}")
endforeach()

forbid_glob("src/kernel/mempool*")
forbid_glob("src/kernel/txmempool*")

foreach(relative_path IN ITEMS
    src/chainstate.cpp
    src/chainstate.h
    src/validation/block_validation.h)
  forbid_text("${relative_path}" "<node/txmempool.h>")
  forbid_text("${relative_path}" "<node/mempool_chain_sync.h>")
  forbid_text("${relative_path}" "<node/mempool_validation.h>")
  forbid_text("${relative_path}" "CTxMemPool")
  forbid_text("${relative_path}" "GetMempool")
  forbid_text("${relative_path}" "m_mempool")
  forbid_text("${relative_path}" "MemPoolOptions")
  forbid_text("${relative_path}" "MemPoolLimits")
  forbid_text("${relative_path}" "MemPoolRemovalReason")
  forbid_text("${relative_path}" "MempoolValidationResult")
  forbid_text("${relative_path}" "MempoolValidationState")
  forbid_text("${relative_path}" "node::MempoolChainSync")
endforeach()

foreach(relative_path IN ITEMS
    src/chainstate.cpp
    src/chainstate.h)
  forbid_text("${relative_path}" "AcceptToMemoryPool")
  forbid_text("${relative_path}" "MempoolAcceptResult")
  forbid_text("${relative_path}" "UpdateMempoolForReorg")
endforeach()

foreach(relative_path IN ITEMS
    src/kernel/CMakeLists.txt)
  forbid_text("${relative_path}" "node/mempool_chain_sync.cpp")
  forbid_text("${relative_path}" "node/mempool_validation.cpp")
  forbid_text("${relative_path}" "node/txmempool.cpp")
  forbid_text("${relative_path}" "node/mempool_removal_reason.cpp")
  forbid_text("${relative_path}" "policy/ephemeral_policy.cpp")
  forbid_text("${relative_path}" "policy/packages.cpp")
  forbid_text("${relative_path}" "policy/rbf.cpp")
  forbid_text("${relative_path}" "policy/truc_policy.cpp")
  forbid_text("${relative_path}" "txgraph.cpp")
endforeach()

require_text("src/CMakeLists.txt" "add_library(bitcoin_mempool_policy")
require_text("src/node/mempool_validation.h" "const MempoolValidationState m_state")
require_text("src/node/txdownloadman.h" "const MempoolValidationState& state")
require_text("src/policy/ephemeral_policy.h" "MempoolValidationState& state")

foreach(relative_path IN ITEMS
    src/node/mempool_validation.h
    src/node/txdownloadman.h
    src/policy/ephemeral_policy.h)
  forbid_text("${relative_path}" "TxValidationState")
  forbid_text("${relative_path}" "TxValidationResult")
endforeach()

foreach(relative_path IN ITEMS
    src/kernel/bitcoinkernel.cpp
    src/kernel/bitcoinkernel.h
    src/kernel/bitcoinkernel_wrapper.h)
  forbid_text("${relative_path}" "MEMPOOL_POLICY")
  forbid_text("${relative_path}" "NO_MEMPOOL")
endforeach()
