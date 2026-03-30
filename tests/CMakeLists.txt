enable_testing()

add_executable(protocol_tests
    unit/test_protocol.cpp
    unit/test_tcp_server_block.cpp
    unit/test_tcp_client_block.cpp
    unit/test_generator.cpp
    unit/test_normalize.cpp
    unit/test_blocked_bounded_queue.cpp
    unit/test_frame_pool.cpp
)
target_link_libraries(protocol_tests
    GTest::gtest_main
    protocol_lib
    common_lib
    generator_lib
    engine_v2_lib
)

include(GoogleTest)
gtest_discover_tests(protocol_tests)
