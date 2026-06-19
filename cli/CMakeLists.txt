add_executable(cpp-can-cli main.cpp)
target_link_libraries(cpp-can-cli PRIVATE cppcan_lib)
target_include_directories(cpp-can-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
