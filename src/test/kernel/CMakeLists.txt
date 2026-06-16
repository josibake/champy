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
