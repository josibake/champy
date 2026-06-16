# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

if(BUILD_FOR_FUZZING)
  message(WARNING "BUILD_FOR_FUZZING=ON will disable all other targets and force BUILD_FUZZ_BINARY=ON.")
  set(BUILD_BITCOIN_BIN OFF)
  set(BUILD_DAEMON OFF)
  set(BUILD_CLI OFF)
  set(BUILD_UTIL_CHAINSTATE OFF)
  set(BUILD_KERNEL_LIB OFF)
  set(BUILD_KERNEL_TEST OFF)
  set(WITH_EMBEDDED_ASMAP OFF)
  set(BUILD_TESTS OFF)
  set(BUILD_BENCH OFF)
  set(BUILD_FUZZ_BINARY ON)

  target_compile_definitions(core_interface INTERFACE
    FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  )
endif()

# Define sanitize_interface with -fsanitize flags intended to apply to all
# libraries and executables.
add_library(sanitize_interface INTERFACE)
target_link_libraries(core_interface INTERFACE sanitize_interface)
if(SANITIZERS)
  # Transform list of sanitizers into -fsanitize flags, replacing "fuzzer" with
  # "fuzzer-no-link" in sanitize_interface flags, and moving "fuzzer" to
  # fuzzer_interface flags. If -DSANITIZERS=fuzzer is specified, the fuzz test
  # binary should be built with -fsanitize=fuzzer (so it can use libFuzzer's
  # main function), but libraries should be built with -fsanitize=fuzzer-no-link
  # (so they can be linked into other executables that have their own main
  # functions).
  string(REGEX REPLACE "(^|,)fuzzer($|,)" "\\1fuzzer-no-link\\2" sanitize_opts "${SANITIZERS}")
  set(fuzz_flag "")
  if(NOT sanitize_opts STREQUAL SANITIZERS)
    set(fuzz_flag "-fsanitize=fuzzer")
  endif()

  # First check if the compiler accepts flags. If an incompatible pair like
  # -fsanitize=address,thread is used here, this check will fail. This will also
  # fail if a bad argument is passed, e.g. -fsanitize=undfeined.
  try_append_cxx_flags("-fsanitize=${sanitize_opts}" TARGET sanitize_interface
    RESULT_VAR cxx_supports_sanitizers
    SKIP_LINK
  )
  if(NOT cxx_supports_sanitizers)
    message(FATAL_ERROR "Compiler did not accept requested flags.")
  endif()

  # Some compilers (e.g. GCC) require additional libraries like libasan,
  # libtsan, libubsan, etc. Make sure linking still works with the sanitize
  # flag. This is a separate check so we can give a better error message when
  # the sanitize flags are supported by the compiler but the actual sanitizer
  # libs are missing.
  try_append_linker_flag("-fsanitize=${sanitize_opts}" VAR SANITIZER_LDFLAGS
    SOURCE "
      #include <cstdint>
      #include <cstddef>
      extern \"C\" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) { return 0; }
      int main() { return 0; }
    "
    RESULT_VAR linker_supports_sanitizers
    NO_CACHE_IF_FAILED
  )
  if(NOT linker_supports_sanitizers)
    message(FATAL_ERROR "Linker did not accept requested flags, you are missing required libraries.")
  endif()
endif()
target_link_options(sanitize_interface INTERFACE ${SANITIZER_LDFLAGS})

# Define fuzzer_interface with flags intended to apply to the fuzz test binary,
# and perform a test compilation to determine correct value of
# FUZZ_BINARY_LINKS_WITHOUT_MAIN_FUNCTION.
if(BUILD_FUZZ_BINARY)
  include(CheckSourceCompilesWithFlags)
  check_cxx_source_compiles_with_flags("
      #include <cstdint>
      #include <cstddef>
      extern \"C\" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) { return 0; }
      // No main() function.
    " FUZZ_BINARY_LINKS_WITHOUT_MAIN_FUNCTION
    LDFLAGS ${SANITIZER_LDFLAGS} ${fuzz_flag}
    LINK_LIBRARIES ${FUZZ_LIBS}
  )
  add_library(fuzzer_interface INTERFACE)
  target_link_options(fuzzer_interface INTERFACE ${fuzz_flag})
  target_link_libraries(fuzzer_interface INTERFACE ${FUZZ_LIBS})
endif()
