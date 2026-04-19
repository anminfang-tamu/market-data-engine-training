cmake_minimum_required(VERSION 3.20)

project(md-engine-training VERSION 0.1 LANGUAGES CXX)

include(CTest)

# Default build type for single-config generator (Ninja)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type")
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Project-wide C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)

if (BUILD_TESTING)
    if (EXISTS "${PROJECT_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
        add_subdirectory(third_party/googletest)
        add_subdirectory(tests)
    else()
        message(FATAL_ERROR
            "BUILD_TESTING=ON, but third_party/googletest is not populated. "
            "Run 'git submodule update --init --recursive' or configure with "
            "-DBUILD_TESTING=OFF."
        )
    endif()
endif()
