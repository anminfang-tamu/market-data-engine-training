enable_testing()

add_executable(protocol_tests
    unit/test_protocol.cpp
)
target_link_libraries(protocol_tests
    GTest::gtest_main
    protocol_lib
)

include(GoogleTest)
gtest_discover_tests(protocol_tests)
