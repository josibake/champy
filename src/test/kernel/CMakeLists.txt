enable_language(C)

add_executable(test_kernel
    test_kernel.cpp
)

target_link_libraries(test_kernel
  PRIVATE
    core_interface
    bitcoinkernel
    Boost::headers
)

add_windows_application_manifest(test_kernel)

add_test(NAME test_kernel COMMAND test_kernel)

add_executable(test_kernel_api_consumer
    kernel_api_consumer.cpp
)

target_link_libraries(test_kernel_api_consumer
  PRIVATE
    bitcoinkernel
)
target_include_directories(test_kernel_api_consumer
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src/kernel
)

add_test(NAME kernel_api_consumer COMMAND test_kernel_api_consumer)
set_tests_properties(kernel_api_consumer
  PROPERTIES
    LABELS "experimental_c_abi;non_release"
)

add_executable(test_bitcoin_kernel_api_consumer
    bitcoin_kernel_api_consumer.cpp
)

target_link_libraries(test_bitcoin_kernel_api_consumer
  PRIVATE
    bitcoin_kernel
)

add_test(NAME bitcoin_kernel_api_consumer COMMAND test_bitcoin_kernel_api_consumer)
set_tests_properties(bitcoin_kernel_api_consumer
  PROPERTIES
    LABELS "experimental_kernel;non_release"
)

add_executable(test_c_api_validation_consumer
    c_api_validation_consumer.c
)

target_link_libraries(test_c_api_validation_consumer
  PRIVATE
    bitcoinkernel
)
target_include_directories(test_c_api_validation_consumer
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src/kernel
)

add_test(NAME c_api_validation_consumer COMMAND test_c_api_validation_consumer)
set_tests_properties(c_api_validation_consumer
  PROPERTIES
    LABELS "experimental_c_abi;non_release"
)
