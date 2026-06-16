# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED INSTALL_PREFIX)
  set(INSTALL_PREFIX "${BUILD_DIR}/test/bitcoin_validation_installed_package/prefix")
endif()

if(NOT DEFINED CONSUMER_BUILD_DIR)
  set(CONSUMER_BUILD_DIR "${BUILD_DIR}/test/bitcoin_validation_installed_package/build")
endif()

file(REMOVE_RECURSE "${INSTALL_PREFIX}" "${CONSUMER_BUILD_DIR}")

set(install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}" --component bitcoin_validation)
if(DEFINED TEST_CONFIGURATION AND NOT TEST_CONFIGURATION STREQUAL "")
  list(APPEND install_command --config "${TEST_CONFIGURATION}")
endif()

execute_process(
  COMMAND ${install_command}
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "bitcoin_validation install failed:\n${install_output}\n${install_error}")
endif()

file(GLOB bitcoin_validation_package_dirs
  LIST_DIRECTORIES true
  "${INSTALL_PREFIX}/lib*/cmake/BitcoinValidation"
  "${INSTALL_PREFIX}/share/cmake/BitcoinValidation"
)
list(LENGTH bitcoin_validation_package_dirs package_dir_count)
if(NOT package_dir_count EQUAL 1)
  message(FATAL_ERROR
    "expected exactly one installed BitcoinValidation package directory, found ${package_dir_count}: "
    "${bitcoin_validation_package_dirs}")
endif()
list(GET bitcoin_validation_package_dirs 0 bitcoin_validation_package_dir)

file(GLOB_RECURSE installed_files
  RELATIVE "${INSTALL_PREFIX}"
  "${INSTALL_PREFIX}/*"
)
foreach(installed_file IN LISTS installed_files)
  foreach(forbidden_path IN ITEMS
      "bitcoin/kernel/"
      "kernel/bitcoinkernel.h"
      "libbitcoin_kernel"
      "libbitcoinkernel"
      "libbitcoin_validation_private"
      "libbitcoin_consensus"
      "libbitcoin_crypto")
    string(FIND "${installed_file}" "${forbidden_path}" forbidden_path_index)
    if(NOT forbidden_path_index EQUAL -1)
      message(FATAL_ERROR "installed bitcoin_validation package contains forbidden file: ${installed_file}")
    endif()
  endforeach()
endforeach()

file(GLOB package_metadata_files
  "${bitcoin_validation_package_dir}/*.cmake"
)
foreach(package_metadata_file IN LISTS package_metadata_files)
  file(READ "${package_metadata_file}" package_metadata)
  foreach(forbidden_text IN ITEMS
      "Bitcoin::bitcoin_kernel"
      "bitcoin_validation_private"
      "bitcoin_validation_script_engine"
      "bitcoin_consensus"
      "bitcoin_crypto"
      "core_interface"
      "kernel_warn_interface"
      "LINK_ONLY:>"
      "kernel/bitcoinkernel.h")
    string(FIND "${package_metadata}" "${forbidden_text}" forbidden_text_index)
    if(NOT forbidden_text_index EQUAL -1)
      message(FATAL_ERROR
        "installed bitcoin_validation package metadata contains forbidden text '${forbidden_text}' in ${package_metadata_file}")
    endif()
  endforeach()
endforeach()

set(configure_command
  "${CMAKE_COMMAND}"
    -S "${SOURCE_DIR}/src/test/bitcoin_validation_installed_package_consumer"
    -B "${CONSUMER_BUILD_DIR}"
    -DBitcoinValidation_DIR=${bitcoin_validation_package_dir}
)
if(DEFINED libsecp256k1_DIR AND NOT libsecp256k1_DIR STREQUAL "")
  list(APPEND configure_command -Dlibsecp256k1_DIR=${libsecp256k1_DIR})
endif()

execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "installed bitcoin_validation consumer configure failed:\n${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${CONSUMER_BUILD_DIR}" --target bitcoin_validation_installed_package_consumer
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "installed bitcoin_validation consumer build failed:\n${build_output}\n${build_error}")
endif()

execute_process(
  COMMAND "${CONSUMER_BUILD_DIR}/bitcoin_validation_installed_package_consumer"
  RESULT_VARIABLE consumer_result
  OUTPUT_VARIABLE consumer_output
  ERROR_VARIABLE consumer_error
)
if(NOT consumer_result EQUAL 0)
  message(FATAL_ERROR "installed bitcoin_validation consumer execution failed:\n${consumer_output}\n${consumer_error}")
endif()
