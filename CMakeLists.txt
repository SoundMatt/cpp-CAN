cmake_minimum_required(VERSION 3.21)

project(cppcan
    VERSION 0.2.0
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

# can/socketcan/bus.hpp — Linux SocketCAN transport. Depends on kernel uapi
# headers (<linux/can.h> etc.) that only exist on Linux; compiled there
# unconditionally (no vcan/can-utils needed to *compile* it, only to open a
# real interface at runtime) so it is always available as a first-class
# transport on Linux, independent of CPPCAN_INTEROP_TESTS below.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(cppcan_lib PRIVATE src/socketcan/bus.cpp)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────

option(CPPCAN_BUILD_TESTS "Build tests" ON)
if(CPPCAN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── CLI binary ────────────────────────────────────────────────────────────────

option(CPPCAN_BUILD_CLI "Build RELAY convert CLI" ON)
if(CPPCAN_BUILD_CLI)
    add_subdirectory(cli)
endif()

# ── Interop testing infrastructure (ROADMAP.md "Interop testing") ──────────────

# Live SocketCAN vcan0 interop test suite + its cpp-can-interop-peer helper
# binary. Off by default: needs a real vcan0 interface (`sudo modprobe vcan
# && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0`) and,
# for the third-party-peer tests, can-utils (candump/cangen) — neither of
# which the default cross-platform build-and-test matrix provides. Mirrors
# rust-DDS's `cyclone-interop` Cargo feature / go-DDS's `interop` build tag:
# without this option the interop targets are not even added to the build,
# so they are absent from ordinary `cmake --build build` / `ctest` runs.
option(CPPCAN_INTEROP_TESTS "Build the live SocketCAN vcan0 interop test suite (Linux + can-utils only)" OFF)
if(CPPCAN_INTEROP_TESTS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "CPPCAN_INTEROP_TESTS requires Linux (SocketCAN, vcan, can-utils)")
    endif()
    add_subdirectory(interop)
endif()

message(STATUS "cpp-CAN v${PROJECT_VERSION} — C++${CMAKE_CXX_STANDARD} / ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
