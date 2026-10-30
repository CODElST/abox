cmake_minimum_required(VERSION 3.16)

project(abox VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
option(ABOX_INSTALL_SYSTEMD "Install the optional aboxd systemd unit" OFF)

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
    src/config.cpp
    src/config.hpp
    src/setup.cpp
    src/setup.hpp
    src/namespace.cpp
    src/namespace.hpp
)

add_executable(aboxd src/daemon.cpp src/cgroup.cpp src/cgroup.hpp)
target_include_directories(aboxd PRIVATE src)

install(TARGETS abox aboxd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES etc/abox.conf
    DESTINATION /etc/abox)
install(FILES README.md PACKAGING.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(ABOX_INSTALL_SYSTEMD)
    configure_file(systemd/aboxd.service.in
        "${CMAKE_CURRENT_BINARY_DIR}/aboxd.service" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/aboxd.service"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
endif()

target_include_directories(abox PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

enable_testing()
add_executable(stats_test 
    tests/stats_test.cpp 
    src/cgroup.cpp)
    
target_include_directories(stats_test PRIVATE src)
add_test(NAME cgroup_stats COMMAND stats_test)

add_executable(config_test tests/config_test.cpp src/config.cpp)
target_include_directories(config_test PRIVATE src)
target_compile_features(config_test PRIVATE cxx_std_20)
add_test(NAME rootfs_configuration COMMAND config_test)
set_tests_properties(rootfs_configuration PROPERTIES LABELS "phaseB;unprivileged")

add_executable(setup_test tests/setup_test.cpp src/setup.cpp)
target_include_directories(setup_test PRIVATE src)
target_compile_features(setup_test PRIVATE cxx_std_20)
add_test(NAME setup_safety COMMAND setup_test)
set_tests_properties(setup_safety PROPERTIES LABELS "phaseC;unprivileged")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/package_test.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/package_test.cmake"
    @ONLY)
add_test(NAME deb_package_validation
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/package_test.cmake")
set_tests_properties(deb_package_validation PROPERTIES LABELS "phaseD;unprivileged")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/install_test.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/install_test.cmake"
    @ONLY)
add_test(NAME install_staging
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/install_test.cmake")
set_tests_properties(install_staging PROPERTIES LABELS "phaseA;unprivileged")

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

set(CPACK_PACKAGE_NAME "abox")
set(CPACK_PACKAGE_VENDOR "CODElST")
set(CPACK_PACKAGE_CONTACT "ABox maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Linux-native experimental sandbox runtime")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "abox-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_SET_DESTDIR ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ABox maintainers")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
