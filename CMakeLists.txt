# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Ubuntu 22.04 LTS Jammy Jellyfish, https://wiki.ubuntu.com/Releases, EOSS in June 2027:
#  - CMake 3.22.1, https://packages.ubuntu.com/jammy/cmake
#
# Centos Stream 9, https://www.centos.org/cl-vs-cs/#end-of-life, EOL in May 2027:
#  - CMake 3.26.5, https://mirror.stream.centos.org/9-stream/AppStream/x86_64/os/Packages/
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed.")
endif()

if(POLICY CMP0171)
  # `codegen` is a reserved target name.
  # See: https://cmake.org/cmake/help/latest/policy/CMP0171.html
  cmake_policy(SET CMP0171 NEW)
endif()

# When adjusting CMake flag variables, we must not override those explicitly
# set by the user. These are a subset of the CACHE_VARIABLES property.
get_directory_property(precious_variables CACHE_VARIABLES)

#=============================
# Project / Package metadata
#=============================
set(CLIENT_NAME "Bitcoin Core")
set(CLIENT_VERSION_MAJOR 31)
set(CLIENT_VERSION_MINOR 99)
set(CLIENT_VERSION_BUILD 0)
set(CLIENT_VERSION_RC 0)
set(CLIENT_VERSION_IS_RELEASE "false")
set(COPYRIGHT_YEAR "2026")

project(BitcoinCore
  VERSION ${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_BUILD}
  DESCRIPTION "Bitcoin client software"
  HOMEPAGE_URL "https://bitcoincore.org/"
  LANGUAGES NONE
)

set(CLIENT_VERSION_STRING ${PROJECT_VERSION})
if(CLIENT_VERSION_RC GREATER 0)
  string(APPEND CLIENT_VERSION_STRING "rc${CLIENT_VERSION_RC}")
endif()

set(COPYRIGHT_HOLDERS "The %s developers")
set(COPYRIGHT_HOLDERS_FINAL "The ${CLIENT_NAME} developers")
set(CLIENT_BUGREPORT "https://github.com/bitcoin/bitcoin/issues")

#=============================
# Language setup
#=============================
if(CMAKE_VERSION VERSION_LESS 4.2 AND CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_HOST_APPLE)
  # We do not use the install_name_tool when cross-compiling for macOS.
  # However, CMake < 4.2 still searches for Apple's version of the tool,
  # which causes an error during configuration.
  # See:
  # - https://gitlab.kitware.com/cmake/cmake/-/issues/27069
  # - https://gitlab.kitware.com/cmake/cmake/-/merge_requests/10955
  # So disable this tool check in further enable_language() commands.
  set(CMAKE_INSTALL_NAME_TOOL "${CMAKE_COMMAND} -E true")
endif()
enable_language(CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/module)
include(ProcessConfigurations)

# Flatten static lib dependencies.
# Without this, if libfoo.a depends on libbar.a, libfoo's objects can't begin
# to be compiled until libbar.a has been created.
if (NOT DEFINED CMAKE_OPTIMIZE_DEPENDENCIES)
  set(CMAKE_OPTIMIZE_DEPENDENCIES TRUE)
endif()

#=============================
# Configurable options
#=============================
include(CMakeDependentOption)
# When adding a new option, end the <help_text> with a full stop for consistency.
option(BUILD_BITCOIN_BIN "Build bitcoin executable." ON)
option(BUILD_DAEMON "Build bitcoind executable." ON)
option(BUILD_CLI "Build bitcoin-cli executable." ON)

option(BUILD_TESTS "Build test_bitcoin and other unit test executables." ON)

option(BUILD_UTIL_CHAINSTATE "Build experimental bitcoin-chainstate executable." OFF)
option(BUILD_KERNEL_LIB "Build experimental bitcoinkernel library." ${BUILD_UTIL_CHAINSTATE})
cmake_dependent_option(BUILD_KERNEL_TEST "Build tests for the experimental bitcoinkernel library." ON "BUILD_KERNEL_LIB" OFF)

option(REDUCE_EXPORTS "Attempt to reduce exported symbols in the resulting executables." OFF)
option(BITCOIN_ENABLE_HARDENING "Enable project hardening compile and link options." OFF)
option(CMAKE_COMPILE_WARNING_AS_ERROR "Treat compiler warnings as errors." OFF)

option(WITH_EMBEDDED_ASMAP "Embed default ASMap data." ON)

option(WITH_USDT "Enable tracepoints for Userspace, Statically Defined Tracing." OFF)
if(WITH_USDT)
  find_package(USDT MODULE REQUIRED)
endif()

option(WITH_EXTERNAL_LIBMULTIPROCESS "Build with an externally provided libmultiprocess package." OFF)
if(WITH_EXTERNAL_LIBMULTIPROCESS)
  find_package(Libmultiprocess REQUIRED COMPONENTS Lib)
  find_package(LibmultiprocessNative REQUIRED COMPONENTS Bin
    NAMES Libmultiprocess
  )
endif()

option(BUILD_BENCH "Build bench_bitcoin executable." OFF)
option(BUILD_FUZZ_BINARY "Build fuzz binary." OFF)
option(BUILD_FOR_FUZZING "Build for fuzzing. Enabling this will disable all other targets and override BUILD_FUZZ_BINARY." OFF)

# Check required C++ toolchain features early.
include(CheckCXXFeatures)
check_cxx_features()

set(configure_warnings)

include(CheckLinkerSupportsPIE)
check_linker_supports_pie(configure_warnings)

# The core_interface library aims to encapsulate common build flags for project
# targets.
add_library(core_interface INTERFACE)
add_library(core_interface_relwithdebinfo INTERFACE)
add_library(core_interface_debug INTERFACE)
target_link_libraries(core_interface INTERFACE
  $<$<CONFIG:RelWithDebInfo>:core_interface_relwithdebinfo>
  $<$<CONFIG:Debug>:core_interface_debug>
)

include(TryAppendCXXFlags)
include(TryAppendLinkerFlag)

# Redefine/adjust per-configuration flags.
target_compile_definitions(core_interface_debug INTERFACE
  DEBUG
  DEBUG_LOCKORDER
  DEBUG_LOCKCONTENTION
  RPC_DOC_CHECK
  ABORT_ON_FAILED_ASSUME
)

include(cmake/platform.cmake)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(core_interface INTERFACE
  Threads::Threads
)

include(cmake/sanitizers.cmake)

include(AddBoostIfNeeded)
add_boost_if_needed()

include(cmake/introspection.cmake)

include(cmake/warnings.cmake)

include(cmake/hardening.cmake)

# Prefer Unix-style package components over frameworks on macOS.
# This improves compatibility with Python version managers.
set(Python3_FIND_FRAMEWORK LAST CACHE STRING "")
# Search for generic names before more specialized ones. This
# improves compatibility with Python version managers that use shims.
set(Python3_FIND_UNVERSIONED_NAMES FIRST CACHE STRING "")
mark_as_advanced(Python3_FIND_FRAMEWORK Python3_FIND_UNVERSIONED_NAMES)
find_package(Python3 3.10 COMPONENTS Interpreter)
if(NOT TARGET Python3::Interpreter)
  list(APPEND configure_warnings
    "Minimum required Python not found."
  )
endif()

if(BUILD_TESTS)
  enable_testing()
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
  # have "make test" depend on "make all"
  set(CMAKE_SKIP_TEST_ALL_DEPENDENCY FALSE)
endif()

add_subdirectory(test)
add_subdirectory(src)

include(cmake/configure_summary.cmake)
print_configure_summary()

# We want all build properties to be encapsulated properly.
include(WarnAboutGlobalProperties)
