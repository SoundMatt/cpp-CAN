add_executable(cppcan_tests
    test_can.cpp
    test_relay.cpp
    test_virtual.cpp
    test_relay_adapter.cpp
    test_isotp.cpp
    test_j1939.cpp
    test_safety.cpp
    test_dbc.cpp
    test_security.cpp
)

target_link_libraries(cppcan_tests
    PRIVATE
        cppcan_lib
        Catch2::Catch2WithMain
)

target_include_directories(cppcan_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

include(CTest)
include(Catch)
catch_discover_tests(cppcan_tests)
