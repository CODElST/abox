cmake_minimum_required(VERSION 3.16)

project(abox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
)

add_executable(abox 
    src/main.cpp
    src/process.cpp
    src/cgroup.cpp
    src/cgroup.hpp
)

target_include_directories(abox PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

enable_testing()
add_executable(stats_test 
    tests/stats_test.cpp 
    src/cgroup.cpp)
    
target_include_directories(stats_test PRIVATE src)
add_test(NAME cgroup_stats COMMAND stats_test)
