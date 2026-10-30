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
    src/namespace.cpp
    src/namespace.hpp
)

add_executable(aboxd src/daemon.cpp src/cgroup.cpp src/cgroup.hpp)
target_include_directories(aboxd PRIVATE src)

target_include_directories(abox PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

enable_testing()
add_executable(stats_test 
    tests/stats_test.cpp 
    src/cgroup.cpp)
    
target_include_directories(stats_test PRIVATE src)
add_test(NAME cgroup_stats COMMAND stats_test)

add_executable(namespace_test
    tests/namespace_test.cpp
    src/namespace.cpp)
target_include_directories(namespace_test PRIVATE src)
add_test(NAME namespace_supervisor COMMAND namespace_test)

add_executable(namespace_acceptance_test tests/namespace_acceptance.cpp)
target_compile_features(namespace_acceptance_test PRIVATE cxx_std_20)
add_executable(cli_acceptance_test tests/cli_acceptance.cpp)
target_compile_features(cli_acceptance_test PRIVATE cxx_std_20)
add_executable(daemon_acceptance_test tests/daemon_acceptance.cpp)
target_compile_features(daemon_acceptance_test PRIVATE cxx_std_20)
add_test(NAME daemon_lifecycle_acceptance
    COMMAND daemon_acceptance_test $<TARGET_FILE:aboxd> $<TARGET_FILE:abox>)
set_tests_properties(daemon_lifecycle_acceptance PROPERTIES
    LABELS "phase4;unprivileged"
    SKIP_RETURN_CODE 77)
foreach(cli_case help version invalid missing bad_option)
    add_test(NAME cli_${cli_case}
        COMMAND cli_acceptance_test $<TARGET_FILE:abox> ${cli_case})
    set_tests_properties(cli_${cli_case} PROPERTIES LABELS "regression;unprivileged")
endforeach()
foreach(ns_case uts pid mount user ipc cgroup rootfs)
    add_test(NAME ns_${ns_case}_acceptance
        COMMAND namespace_acceptance_test $<TARGET_FILE:abox> ${ns_case})
    set_tests_properties(ns_${ns_case}_acceptance PROPERTIES
        LABELS "phase3;requires_privileges"
        SKIP_RETURN_CODE 77)
endforeach()
