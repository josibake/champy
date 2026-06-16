# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

if(BITCOIN_ENABLE_HARDENING)
  if(MSVC)
    try_append_linker_flag("/DYNAMICBASE" TARGET core_interface)
    try_append_linker_flag("/HIGHENTROPYVA" TARGET core_interface)
    try_append_linker_flag("/NXCOMPAT" TARGET core_interface)
  else()

    # _FORTIFY_SOURCE requires that there is some level of optimization,
    # otherwise it does nothing and just creates a compiler warning.
    try_append_cxx_flags("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3"
      RESULT_VAR cxx_supports_fortify_source
      SOURCE "int main() {
              # if !defined __OPTIMIZE__ || __OPTIMIZE__ <= 0
                #error
              #endif
              }"
    )
    if(cxx_supports_fortify_source)
      target_compile_options(core_interface INTERFACE
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=3
      )
    endif()
    unset(cxx_supports_fortify_source)

    try_append_cxx_flags("-Wstack-protector" TARGET core_interface SKIP_LINK)
    try_append_cxx_flags("-fstack-protector-all" TARGET core_interface)
    try_append_cxx_flags("-fcf-protection=full" TARGET core_interface)

    if(MINGW)
      # stack-clash-protection is a no-op for Windows.
      # See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90458 for more details.
    else()
      try_append_cxx_flags("-fstack-clash-protection" TARGET core_interface)
    endif()

    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        try_append_cxx_flags("-mbranch-protection=bti" TARGET core_interface SKIP_LINK)
      else()
        try_append_cxx_flags("-mbranch-protection=standard" TARGET core_interface SKIP_LINK)
      endif()
    endif()

    try_append_linker_flag("-Wl,--enable-reloc-section" TARGET core_interface)
    try_append_linker_flag("-Wl,--dynamicbase" TARGET core_interface)
    try_append_linker_flag("-Wl,--nxcompat" TARGET core_interface)
    try_append_linker_flag("-Wl,--high-entropy-va" TARGET core_interface)
    try_append_linker_flag("-Wl,-z,relro" TARGET core_interface)
    try_append_linker_flag("-Wl,-z,now" TARGET core_interface)
    # TODO: This can be dropped once Bitcoin Core no longer supports
    #       NetBSD 10.0 or if upstream fix is backported.
    # NetBSD's dynamic linker ld.elf_so < 11.0 supports exactly 2
    # `PT_LOAD` segments and binaries linked with `-z separate-code`
    # have 4 `PT_LOAD` segments.
    # Relevant discussions:
    # - https://github.com/bitcoin/bitcoin/pull/28724#issuecomment-2589347934
    # - https://mail-index.netbsd.org/tech-userlevel/2023/01/05/msg013666.html
    if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD" AND CMAKE_SYSTEM_VERSION VERSION_LESS 11.0)
      try_append_linker_flag("-Wl,-z,noseparate-code" TARGET core_interface)
    else()
      try_append_linker_flag("-Wl,-z,separate-code" TARGET core_interface)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      try_append_linker_flag("-Wl,-fixup_chains" TARGET core_interface)
    endif()
  endif()
endif()

if(REDUCE_EXPORTS)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  try_append_linker_flag("-Wl,--exclude-libs,ALL" TARGET core_interface)
  try_append_linker_flag("-Wl,-no_exported_symbols" VAR CMAKE_EXE_LINKER_FLAGS)
endif()
