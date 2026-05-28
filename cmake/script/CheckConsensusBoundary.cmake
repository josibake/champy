# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB_RECURSE consensus_files
  "${SOURCE_DIR}/src/consensus/*.cpp"
  "${SOURCE_DIR}/src/consensus/*.h"
)

function(extract_cmake_call out_var call_name target_name)
  file(READ "${SOURCE_DIR}/src/CMakeLists.txt" src_cmake)
  string(FIND "${src_cmake}" "${call_name}(${target_name}" call_start)
  if(call_start EQUAL -1)
    message(FATAL_ERROR "Could not find ${call_name}(${target_name}...) in src/CMakeLists.txt")
  endif()

  string(SUBSTRING "${src_cmake}" ${call_start} -1 call_tail)
  string(FIND "${call_tail}" "\n)" call_end)
  if(call_end EQUAL -1)
    message(FATAL_ERROR "Could not find end of ${call_name}(${target_name}...) in src/CMakeLists.txt")
  endif()

  math(EXPR call_length "${call_end} + 2")
  string(SUBSTRING "${call_tail}" 0 ${call_length} call_block)
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

set(allowed_public_protocol_headers
  "arith_uint256.h"
  "primitives/block.h"
  "primitives/transaction.h"
  "script/verify_flags.h"
  "uint256.h"
)

set(allowed_source_protocol_headers
  ${allowed_public_protocol_headers}
  "hash.h"
  "script/interpreter.h"
  "script/script.h"
  "script/script_error.h"
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
    string(REGEX MATCHALL "#[ \t]*include[ \t]*<[^>]+>" public_includes "${contents}")
    foreach(include_line IN LISTS public_includes)
      string(REGEX REPLACE ".*<([^>]+)>.*" "\\1" include_header "${include_line}")
      set(allowed_public_include FALSE)
      if(include_header MATCHES "^consensus/")
        set(allowed_public_include TRUE)
      endif()
      list(FIND allowed_public_protocol_headers "${include_header}" protocol_header_index)
      if(NOT protocol_header_index EQUAL -1)
        set(allowed_public_include TRUE)
      endif()
      list(FIND allowed_public_std_headers "${include_header}" std_header_index)
      if(NOT std_header_index EQUAL -1)
        set(allowed_public_include TRUE)
      endif()
      if(NOT allowed_public_include)
        message(FATAL_ERROR "${relative_path} exposes non-consensus public include <${include_header}>")
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
