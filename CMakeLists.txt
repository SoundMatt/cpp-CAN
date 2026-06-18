cmake_minimum_required(VERSION 3.21)

project(cppcan
    VERSION 0.1.0
    DESCRIPTION "C++ CAN bus library — RELAY-conformant"
    HOMEPAGE_URL "https://github.com/SoundMatt/cpp-CAN"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual
        -Wno-unused-parameter
    )
endif()
if(MSVC)
    add_compile_options(
        /W4
        /wd4100   # unreferenced formal parameter
        /wd4996   # deprecated CRT functions
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/FetchDeps.cmake)

# ── Library ───────────────────────────────────────────────────────────────────

add_library(cppcan_lib STATIC
    src/relay.cpp
    src/can.cpp
    src/virtual/bus.cpp
    src/isotp/transport.cpp
    src/j1939/pgn.cpp
    src/safety/e2e.cpp
    src/dbc/parser.cpp
)

target_include_directories(cppcan_lib
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)
target_link_libraries(cppcan_lib PUBLIC Threads::Threads)

# ── Tests ─────────────────────────────────────────────────────────────────────

option(CPPCAN_BUILD_TESTS "Build tests" ON)
if(CPPCAN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "cpp-CAN v${PROJECT_VERSION} — C++${CMAKE_CXX_STANDARD} / ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
