# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

cmake_minimum_required(VERSION 3.22)

project(BitcoinValidationInstalledPackageConsumer LANGUAGES CXX)

find_package(BitcoinValidation CONFIG REQUIRED)

add_executable(bitcoin_validation_installed_package_consumer
  main.cpp
)
target_compile_features(bitcoin_validation_installed_package_consumer
  PRIVATE
    cxx_std_20
)
target_link_libraries(bitcoin_validation_installed_package_consumer
  PRIVATE
    Bitcoin::bitcoin_validation
)
