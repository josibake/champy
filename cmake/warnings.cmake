# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(warn_interface INTERFACE)
target_link_libraries(core_interface INTERFACE warn_interface)
if(MSVC)
  try_append_cxx_flags("/W3" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4018" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4146" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4244" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4267" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4715" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4805" TARGET warn_interface SKIP_LINK)
  target_compile_definitions(warn_interface INTERFACE
    _CRT_SECURE_NO_WARNINGS
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
  )
else()
  try_append_cxx_flags("-Wall" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wextra" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wgnu" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wcovered-switch-default" TARGET warn_interface SKIP_LINK)
  # Some compilers will ignore -Wformat-security without -Wformat, so just combine the two here.
  try_append_cxx_flags("-Wformat -Wformat-security" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wvla" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wshadow-field" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wthread-safety" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wthread-safety-pointer" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wloop-analysis" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wredundant-decls" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wunused-member-function" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wdate-time" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wconditional-uninitialized" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wduplicated-branches" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wduplicated-cond" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wlogical-op" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Woverloaded-virtual" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wsuggest-override" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wimplicit-fallthrough" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wunreachable-code" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wdocumentation" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wself-assign" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wbidi-chars=any" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wundef" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wleading-whitespace=spaces" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wtrailing-whitespace=any" TARGET warn_interface SKIP_LINK)

  # Some compilers (gcc) ignore unknown -Wno-* options, but warn about all
  # unknown options if any other warning is produced. Test the -Wfoo case, and
  # set the -Wno-foo case if it works.
  try_append_cxx_flags("-Wunused-parameter" TARGET warn_interface SKIP_LINK
    IF_CHECK_PASSED "-Wno-unused-parameter"
  )
endif()

# Don't allow extended (non-ASCII) symbols in identifiers. This is easier for code review.
try_append_cxx_flags("-fno-extended-identifiers" TARGET core_interface SKIP_LINK)
