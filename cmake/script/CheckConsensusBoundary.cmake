# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

include("${SOURCE_DIR}/cmake/BitcoinConsensusApi.cmake")

file(GLOB_RECURSE consensus_files
  "${SOURCE_DIR}/src/consensus/*.cpp"
  "${SOURCE_DIR}/src/consensus/*.h"
)
file(GLOB consensus_headers
  RELATIVE "${SOURCE_DIR}/src"
  "${SOURCE_DIR}/src/consensus/*.h"
)

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

function(extract_cmake_call out_var call_name target_name)
  extract_cmake_call_from_file(call_block "${SOURCE_DIR}/src/CMakeLists.txt" "${call_name}" "${target_name}")
  set(${out_var} "${call_block}" PARENT_SCOPE)
endfunction()

set(allowed_consensus_target_sources
  "arith_uint256.cpp"
  "hash.cpp"
  "primitives/block.cpp"
  "primitives/transaction.cpp"
  "pubkey.cpp"
  "script/interpreter.cpp"
  "script/script.cpp"
  "script/script_error.cpp"
  "uint256.cpp"
)

set(allowed_consensus_target_links
  "core_interface"
  "bitcoin_crypto"
  "secp256k1"
)

extract_cmake_call(consensus_target_sources_block "add_library" "bitcoin_consensus")
string(REGEX MATCHALL "[A-Za-z0-9_./+-]+\\.cpp" consensus_target_sources "${consensus_target_sources_block}")
foreach(source IN LISTS consensus_target_sources)
  set(allowed_source FALSE)
  if(source MATCHES "^consensus/")
    set(allowed_source TRUE)
  endif()
  list(FIND allowed_consensus_target_sources "${source}" allowed_source_index)
  if(NOT allowed_source_index EQUAL -1)
    set(allowed_source TRUE)
  endif()
  if(NOT allowed_source)
    message(FATAL_ERROR "bitcoin_consensus target includes non-consensus source ${source}")
  endif()
endforeach()

extract_cmake_call(consensus_target_features_block "target_compile_features" "bitcoin_consensus")
if(NOT consensus_target_features_block MATCHES "PUBLIC"
    OR NOT consensus_target_features_block MATCHES "cxx_std_20")
  message(FATAL_ERROR "bitcoin_consensus must publish its C++20 requirement")
endif()

extract_cmake_call(consensus_target_include_dirs_block "target_include_directories" "bitcoin_consensus")
if(NOT consensus_target_include_dirs_block MATCHES "PUBLIC")
  message(FATAL_ERROR "bitcoin_consensus must publish its build include directories")
endif()
set(required_consensus_include_dirs
  "$<BUILD_INTERFACE:\${CMAKE_CURRENT_BINARY_DIR}>"
  "$<BUILD_INTERFACE:\${CMAKE_CURRENT_SOURCE_DIR}>"
)
foreach(include_dir IN LISTS required_consensus_include_dirs)
  string(FIND "${consensus_target_include_dirs_block}" "${include_dir}" include_dir_index)
  if(include_dir_index EQUAL -1)
    message(FATAL_ERROR "bitcoin_consensus missing public include directory ${include_dir}")
  endif()
endforeach()

extract_cmake_call(consensus_target_properties_block "set_target_properties" "bitcoin_consensus")
set(required_consensus_target_properties
  "BITCOIN_CONSENSUS_PUBLIC_API_HEADERS"
  "BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS"
  "BITCOIN_CONSENSUS_PUBLIC_TRANSITIVE_HEADERS"
  "BITCOIN_CONSENSUS_SUPPORT_HEADERS"
  "BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS"
  "BITCOIN_CONSENSUS_INSTALL_HEADERS"
)
foreach(property IN LISTS required_consensus_target_properties)
  string(FIND "${consensus_target_properties_block}" "${property}" property_index)
  if(property_index EQUAL -1)
    message(FATAL_ERROR "bitcoin_consensus missing target property ${property}")
  endif()
endforeach()

extract_cmake_call(consensus_target_links_block "target_link_libraries" "bitcoin_consensus")
string(REGEX MATCHALL "[A-Za-z0-9_:.-]+" consensus_target_link_tokens "${consensus_target_links_block}")
foreach(token IN LISTS consensus_target_link_tokens)
  if(token STREQUAL "target_link_libraries"
      OR token STREQUAL "bitcoin_consensus"
      OR token STREQUAL "PRIVATE"
      OR token STREQUAL "PUBLIC"
      OR token STREQUAL "INTERFACE")
    continue()
  endif()

  list(FIND allowed_consensus_target_links "${token}" allowed_link_index)
  if(allowed_link_index EQUAL -1)
    message(FATAL_ERROR "bitcoin_consensus target links non-consensus dependency ${token}")
  endif()
endforeach()

extract_cmake_call_from_file(test_bitcoin_sources_block "${SOURCE_DIR}/src/test/CMakeLists.txt" "add_executable" "test_bitcoin")
if(test_bitcoin_sources_block MATCHES "consensus_api_consumer_tests\\.cpp")
  message(FATAL_ERROR "consensus_api_consumer_tests.cpp must build only in test_consensus_api_consumer")
endif()

extract_cmake_call_from_file(consensus_api_consumer_target_sources_block "${SOURCE_DIR}/src/test/CMakeLists.txt" "add_executable" "test_consensus_api_consumer")
if(NOT consensus_api_consumer_target_sources_block MATCHES "consensus_api_consumer_tests\\.cpp")
  message(FATAL_ERROR "test_consensus_api_consumer must build consensus_api_consumer_tests.cpp")
endif()

set(allowed_consensus_api_consumer_target_links
  "bitcoin_consensus"
  "Boost::headers"
)

extract_cmake_call_from_file(consensus_api_consumer_target_links_block "${SOURCE_DIR}/src/test/CMakeLists.txt" "target_link_libraries" "test_consensus_api_consumer")
string(REGEX MATCHALL "[A-Za-z0-9_:.-]+" consensus_api_consumer_target_link_tokens "${consensus_api_consumer_target_links_block}")
foreach(token IN LISTS consensus_api_consumer_target_link_tokens)
  if(token STREQUAL "target_link_libraries"
      OR token STREQUAL "test_consensus_api_consumer"
      OR token STREQUAL "PRIVATE"
      OR token STREQUAL "PUBLIC"
      OR token STREQUAL "INTERFACE")
    continue()
  endif()

  list(FIND allowed_consensus_api_consumer_target_links "${token}" allowed_link_index)
  if(allowed_link_index EQUAL -1)
    message(FATAL_ERROR "test_consensus_api_consumer links non-consensus dependency ${token}")
  endif()
endforeach()

set(forbidden_include_prefixes
  "interfaces"
  "kernel"
  "node"
  "util"
)

set(forbidden_include_headers
  "block_validation.h"
  "chain.h"
  "chainstate.h"
  "coins.h"
  "dbwrapper.h"
  "deploymentstatus.h"
  "flatfile.h"
  "logging.h"
  "sync.h"
  "tinyformat.h"
  "txdb.h"
  "validation.h"
  "validation_state.h"
)

set(forbidden_symbols
  "ArgsManager"
  "BlockManager"
  "BlockValidationState"
  "CBlockIndex"
  "CCoinsView"
  "Chainstate"
  "ChainstateManager"
  "TxValidationState"
  "cs_main"
  "gArgs"
)

set(forbidden_public_api_symbols
  "CCheckQueue"
  "CScriptCheck"
  "PrecomputedTransactionData"
)

set(allowed_public_protocol_headers ${BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS})
set(allowed_source_protocol_headers
  ${BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS}
  ${BITCOIN_CONSENSUS_PUBLIC_TRANSITIVE_HEADERS}
  ${BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS}
)

set(allowed_public_std_headers
  "array"
  "cassert"
  "chrono"
  "cstddef"
  "cstdint"
  "cstdlib"
  "exception"
  "limits"
  "map"
  "memory"
  "optional"
  "span"
  "string"
  "string_view"
  "utility"
  "variant"
  "vector"
)

set(allowed_source_std_headers
  ${allowed_public_std_headers}
  "algorithm"
  "cstdio"
  "cstring"
  "set"
)

set(consensus_public_api_headers ${BITCOIN_CONSENSUS_PUBLIC_API_HEADERS})
set(consensus_support_headers ${BITCOIN_CONSENSUS_SUPPORT_HEADERS})

function(collect_local_header_closure out_var)
  set(visited_headers)
  set(pending_headers ${ARGN})

  while(pending_headers)
    list(POP_FRONT pending_headers header)
    list(FIND visited_headers "${header}" visited_index)
    if(NOT visited_index EQUAL -1)
      continue()
    endif()

    if(NOT EXISTS "${SOURCE_DIR}/src/${header}")
      message(FATAL_ERROR "Install header closure references missing header ${header}")
    endif()

    list(APPEND visited_headers "${header}")
    file(READ "${SOURCE_DIR}/src/${header}" header_contents)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*<[^>]+>" header_includes "${header_contents}")
    foreach(include_line IN LISTS header_includes)
      string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
      if(EXISTS "${SOURCE_DIR}/src/${include_header}")
        list(APPEND pending_headers "${include_header}")
      endif()
    endforeach()
  endwhile()

  set(${out_var} "${visited_headers}" PARENT_SCOPE)
endfunction()

collect_local_header_closure(actual_install_headers ${BITCOIN_CONSENSUS_PUBLIC_API_HEADERS})
list(SORT actual_install_headers)
set(expected_install_headers ${BITCOIN_CONSENSUS_INSTALL_HEADERS})
list(SORT expected_install_headers)
if(NOT "${actual_install_headers}" STREQUAL "${expected_install_headers}")
  message(FATAL_ERROR "BITCOIN_CONSENSUS_INSTALL_HEADERS must exactly match the public API header closure")
endif()

foreach(header IN LISTS BITCOIN_CONSENSUS_INSTALL_HEADERS)
  list(FIND BITCOIN_CONSENSUS_SUPPORT_HEADERS "${header}" support_install_index)
  if(NOT support_install_index EQUAL -1)
    message(FATAL_ERROR "Install header manifest exposes support-only header ${header}")
  endif()
  list(FIND BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS "${header}" internal_install_index)
  if(NOT internal_install_index EQUAL -1)
    message(FATAL_ERROR "Install header manifest exposes internal protocol header ${header}")
  endif()
endforeach()

set(consensus_known_headers
  ${consensus_public_api_headers}
  ${consensus_support_headers}
)
foreach(header IN LISTS consensus_headers)
  list(FIND consensus_known_headers "${header}" known_header_index)
  if(known_header_index EQUAL -1)
    message(FATAL_ERROR "${header} is not classified as a public API or support header")
  endif()
endforeach()

foreach(header IN LISTS consensus_known_headers)
  if(NOT EXISTS "${SOURCE_DIR}/src/${header}")
    message(FATAL_ERROR "Classified consensus header ${header} does not exist")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/consensus/api.h" consensus_api_header)
string(REGEX MATCHALL "#[ \t]*include[ \t]*<consensus/[^>]+>" consensus_api_include_lines "${consensus_api_header}")
set(consensus_api_includes)
foreach(include_line IN LISTS consensus_api_include_lines)
  string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
  list(APPEND consensus_api_includes "${include_header}")
endforeach()
list(SORT consensus_api_includes)

set(expected_consensus_api_includes ${consensus_public_api_headers})
list(REMOVE_ITEM expected_consensus_api_includes "consensus/api.h")
list(SORT expected_consensus_api_includes)
if(NOT "${consensus_api_includes}" STREQUAL "${expected_consensus_api_includes}")
  message(FATAL_ERROR "consensus/api.h direct includes do not match the public API allowlist")
endif()

foreach(header IN LISTS consensus_public_api_headers)
  if(header STREQUAL "consensus/api.h")
    continue()
  endif()

  file(READ "${SOURCE_DIR}/src/${header}" public_header_contents)
  string(REGEX MATCHALL "#[ \t]*include[ \t]*<consensus/[^>]+>" public_consensus_includes "${public_header_contents}")
  foreach(include_line IN LISTS public_consensus_includes)
    string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
    list(FIND consensus_support_headers "${include_header}" support_header_index)
    if(NOT support_header_index EQUAL -1)
      message(FATAL_ERROR "${header} exposes support-only consensus header ${include_header}")
    endif()
  endforeach()
endforeach()

set(actual_public_protocol_headers)
foreach(header IN LISTS consensus_public_api_headers)
  file(READ "${SOURCE_DIR}/src/${header}" public_header_contents)
  string(REGEX MATCHALL "#[ \t]*include[ \t]*<[^>]+>" public_includes "${public_header_contents}")
  foreach(include_line IN LISTS public_includes)
    string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
    list(FIND BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS "${include_header}" protocol_header_index)
    if(NOT protocol_header_index EQUAL -1)
      list(APPEND actual_public_protocol_headers "${include_header}")
    endif()
    list(FIND BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS "${include_header}" internal_header_index)
    if(NOT internal_header_index EQUAL -1)
      message(FATAL_ERROR "${header} exposes internal consensus protocol header ${include_header}")
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES actual_public_protocol_headers)
list(SORT actual_public_protocol_headers)
set(expected_public_protocol_headers ${BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS})
list(SORT expected_public_protocol_headers)
if(NOT "${actual_public_protocol_headers}" STREQUAL "${expected_public_protocol_headers}")
  message(FATAL_ERROR "Public consensus protocol headers do not match the API manifest")
endif()

set(consensus_api_consumer_test "${SOURCE_DIR}/src/test/consensus_api_consumer_tests.cpp")
if(EXISTS "${consensus_api_consumer_test}")
  file(READ "${consensus_api_consumer_test}" consensus_api_consumer_contents)
  string(REGEX MATCHALL "#[ \t]*include[ \t]*<consensus/[^>]+>" consensus_api_consumer_include_lines "${consensus_api_consumer_contents}")
  foreach(include_line IN LISTS consensus_api_consumer_include_lines)
    string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
    if(NOT include_header STREQUAL "consensus/api.h")
      message(FATAL_ERROR "consensus_api_consumer_tests.cpp may include only <consensus/api.h> from the consensus directory")
    endif()
  endforeach()
endif()

foreach(file_path IN LISTS consensus_files)
  file(READ "${file_path}" contents)
  file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${file_path}")

  foreach(prefix IN LISTS forbidden_include_prefixes)
    string(REGEX MATCH "#[ \t]*include[ \t]*<${prefix}/" match "${contents}")
    if(match)
      message(FATAL_ERROR "${relative_path} includes forbidden Core dependency prefix <${prefix}/>")
    endif()
  endforeach()

  foreach(header IN LISTS forbidden_include_headers)
    string(REGEX MATCH "#[ \t]*include[ \t]*<${header}>" match "${contents}")
    if(match)
      message(FATAL_ERROR "${relative_path} includes forbidden Core dependency <${header}>")
    endif()
  endforeach()

  foreach(symbol IN LISTS forbidden_symbols)
    string(REGEX MATCH "(^|[^A-Za-z0-9_])${symbol}([^A-Za-z0-9_]|$)" match "${contents}")
    if(match)
      message(FATAL_ERROR "${relative_path} references forbidden Core symbol ${symbol}")
    endif()
  endforeach()

  string(REGEX MATCHALL "#[ \t]*include[ \t]*<[^>]+>" source_includes "${contents}")
  foreach(include_line IN LISTS source_includes)
    string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
    set(allowed_source_include FALSE)
    if(include_header MATCHES "^consensus/")
      set(allowed_source_include TRUE)
    endif()
    list(FIND allowed_source_protocol_headers "${include_header}" source_protocol_header_index)
    if(NOT source_protocol_header_index EQUAL -1)
      set(allowed_source_include TRUE)
    endif()
    list(FIND allowed_source_std_headers "${include_header}" source_std_header_index)
    if(NOT source_std_header_index EQUAL -1)
      set(allowed_source_include TRUE)
    endif()
    if(NOT allowed_source_include)
      message(FATAL_ERROR "${relative_path} includes non-consensus source dependency <${include_header}>")
    endif()
  endforeach()

  if(relative_path MATCHES "\\.h$")
    set(allowed_header_protocol_headers ${allowed_source_protocol_headers})
    set(allowed_header_std_headers ${allowed_source_std_headers})
    list(FIND consensus_public_api_headers "${relative_path}" public_header_index)
    if(NOT public_header_index EQUAL -1)
      set(allowed_header_protocol_headers ${allowed_public_protocol_headers})
      set(allowed_header_std_headers ${allowed_public_std_headers})
    endif()

    string(REGEX MATCHALL "#[ \t]*include[ \t]*<[^>]+>" public_includes "${contents}")
    foreach(include_line IN LISTS public_includes)
      string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
      set(allowed_public_include FALSE)
      if(include_header MATCHES "^consensus/")
        set(allowed_public_include TRUE)
      endif()
      list(FIND allowed_header_protocol_headers "${include_header}" protocol_header_index)
      if(NOT protocol_header_index EQUAL -1)
        set(allowed_public_include TRUE)
      endif()
      list(FIND allowed_header_std_headers "${include_header}" std_header_index)
      if(NOT std_header_index EQUAL -1)
        set(allowed_public_include TRUE)
      endif()
      if(NOT allowed_public_include)
        message(FATAL_ERROR "${relative_path} exposes non-consensus header include <${include_header}>")
      endif()
    endforeach()

    foreach(symbol IN LISTS forbidden_public_api_symbols)
      string(REGEX MATCH "(^|[^A-Za-z0-9_])${symbol}([^A-Za-z0-9_]|$)" match "${contents}")
      if(match)
        message(FATAL_ERROR "${relative_path} exposes forbidden consensus API symbol ${symbol}")
      endif()
    endforeach()
  endif()
endforeach()
