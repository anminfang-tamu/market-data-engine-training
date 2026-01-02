# Engine lib
add_library(engine_lib
    engine/engine.cpp
    engine/act/order_mock.cpp
    engine/ingest/ingest.cpp
    engine/normalize/normalize.cpp
    engine/process/strategy_mock.cpp
)
target_include_directories(engine_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Engine executable
add_executable(engine_app engine/main.cpp)

# Generator lib
add_library(generator_lib
    generator/generator.cpp
)
target_include_directories(generator_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Generator executable
add_executable(generator_app generator/main.cpp)

# Common lib
add_library(common_lib
    common/logging/logger.cpp
    common/metrics/metrics.cpp
)
target_include_directories(common_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(common_lib PUBLIC cxx_std_20)

# Protocol, Containers libs
add_library(protocol_lib INTERFACE)
target_include_directories(protocol_lib INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    )
add_library(containers_lib INTERFACE)
target_include_directories(containers_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/containers
)

# Link shared dependencies to the libs
target_link_libraries(engine_lib PUBLIC common_lib protocol_lib containers_lib)
target_link_libraries(generator_lib PUBLIC common_lib protocol_lib containers_lib)

# Link app & lib
target_link_libraries(engine_app PRIVATE engine_lib)
target_link_libraries(generator_app PRIVATE generator_lib)