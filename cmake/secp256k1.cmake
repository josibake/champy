# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(find_secp256k1)
  find_package(libsecp256k1 CONFIG REQUIRED)

  if(NOT TARGET libsecp256k1::secp256k1)
    message(FATAL_ERROR "libsecp256k1 package does not provide libsecp256k1::secp256k1")
  endif()

  if(NOT TARGET secp256k1)
    add_library(secp256k1 INTERFACE IMPORTED GLOBAL)
    set_target_properties(secp256k1 PROPERTIES
      INTERFACE_LINK_LIBRARIES libsecp256k1::secp256k1
    )
  endif()
endfunction()
