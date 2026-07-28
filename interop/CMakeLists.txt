# interop/ — live SocketCAN vcan0 interop testing infrastructure.
# Only ever added to the build when CPPCAN_INTEROP_TESTS=ON (see the
# top-level CMakeLists.txt, which also guards this against non-Linux).

# can-interop-peer — standalone writer/reader test-support binary
# (can_interop_peer.cpp), a real OS process driven by production
# can::socketcan::Bus machinery. Used by test_two_process_interop.cpp.
add_executable(cpp-can-interop-peer can_interop_peer.cpp)
target_link_libraries(cpp-can-interop-peer PRIVATE cppcan_lib)
target_include_directories(cpp-can-interop-peer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cli
)

# The live interop test suite itself.
add_executable(cppcan_can_interop_tests
    test_two_process_interop.cpp
    test_cangen_candump_interop.cpp
)
target_link_libraries(cppcan_can_interop_tests
    PRIVATE
        cppcan_lib
        Catch2::Catch2WithMain
)
target_include_directories(cppcan_can_interop_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../cli
)
# Absolute path to the peer binary this test binary was built alongside —
# the CMake equivalent of Cargo's CARGO_BIN_EXE_<name> (see
# test_two_process_interop.cpp).
target_compile_definitions(cppcan_can_interop_tests PRIVATE
    CPPCAN_INTEROP_PEER_BIN="$<TARGET_FILE:cpp-can-interop-peer>"
)
add_dependencies(cppcan_can_interop_tests cpp-can-interop-peer)

enable_testing() # idempotent — safe even if tests/ (CPPCAN_BUILD_TESTS) already called this
include(CTest)
include(Catch)
catch_discover_tests(cppcan_can_interop_tests
    PROPERTIES LABELS "can-interop"
)
