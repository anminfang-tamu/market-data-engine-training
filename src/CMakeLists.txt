# Common libs
add_library(logging_lib
    common/logging/logger.cpp
)
target_include_directories(logging_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(logging_lib PUBLIC cxx_std_20)

add_library(metrics_lib
    common/metrics/metrics.cpp
)
target_include_directories(metrics_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(metrics_lib PUBLIC cxx_std_20)

add_library(udp_sender_lib
    common/net/v2/udp_sender.cpp
)
target_include_directories(udp_sender_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(udp_sender_lib PUBLIC cxx_std_20)

add_library(udp_receiver_lib
    common/net/v2/udp_receiver.cpp
)
target_include_directories(udp_receiver_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(udp_receiver_lib PUBLIC cxx_std_20)

# Protocol, Containers libs
add_library(protocol_lib INTERFACE)
target_include_directories(protocol_lib INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    )
add_library(containers_lib INTERFACE)
target_include_directories(containers_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/containers
)

# Generator lib
add_library(generator_lib
    generator/v1/generator.cpp
)
target_include_directories(generator_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Generator v2 lib
add_library(generator_v2_lib
    generator/v2/generator.cpp
)
target_include_directories(generator_v2_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Generator executable
add_executable(generator_app generator/main.cpp)

target_link_libraries(generator_lib PUBLIC logging_lib udp_sender_lib protocol_lib containers_lib)
target_link_libraries(generator_v2_lib PUBLIC logging_lib udp_sender_lib protocol_lib containers_lib)
target_link_libraries(generator_app PRIVATE generator_v2_lib)

if (NOT MD_BUILD_GENERATOR_ONLY)
    # Engine lib
    add_library(engine_lib
        engine/v1/engine.cpp
        engine/act/order_mock.cpp
        engine/ingest/ingest.cpp
        engine/normalize/normalize.cpp
        engine/process/strategy_mock.cpp
    )
    target_include_directories(engine_lib PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Engine v2 lib
    add_library(engine_v2_lib
        engine/v2/engine.cpp
        engine/act/order_mock.cpp
        engine/ingest/ingest.cpp
        engine/normalize/normalize.cpp
        engine/process/strategy_mock.cpp
    )
    target_include_directories(engine_v2_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)

    # Engine v3 lib
    add_library(engine_v3_lib
        engine/v3/engine.cpp
    )
    target_include_directories(engine_v3_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${DPDK_INCLUDE_DIRS}
    )
    target_compile_options(engine_v3_lib PUBLIC ${DPDK_CFLAGS_OTHER})
    target_link_libraries(engine_v3_lib PUBLIC ${DPDK_LIBRARIES})

    if (DPDK_LIBRARY_DIRS)
        set_target_properties(engine_v3_lib PROPERTIES
            BUILD_RPATH "${DPDK_LIBRARY_DIRS}"
            INSTALL_RPATH "${DPDK_LIBRARY_DIRS}"
        )
    endif()

    # Engine executable
    add_executable(engine_app engine/main.cpp)

    target_link_libraries(engine_v3_lib PUBLIC logging_lib protocol_lib)
    target_link_libraries(engine_lib PUBLIC logging_lib metrics_lib protocol_lib containers_lib)
    target_link_libraries(engine_v2_lib PUBLIC logging_lib metrics_lib udp_receiver_lib protocol_lib containers_lib numa)
    target_link_libraries(engine_app PRIVATE engine_v3_lib)

    if (DPDK_LIBRARY_DIRS)
        set_target_properties(engine_app PROPERTIES
            BUILD_RPATH "${DPDK_LIBRARY_DIRS}"
            INSTALL_RPATH "${DPDK_LIBRARY_DIRS}"
        )
    endif()
endif()
