cmake_minimum_required(VERSION 3.20)

project(
    ThreadPoolPro
    VERSION 1.0.9
    LANGUAGES CXX
)


# ============================================================================
# C++ Configuration
# ============================================================================

# Configure the C++ language standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ============================================================================
# ThreadPoolPro (Header-only Library)
# ============================================================================

# Define the header-only ThreadPoolPro library.
add_library(ThreadPoolPro INTERFACE)

# Expose the public include directory to consumers.
target_include_directories(
    ThreadPoolPro
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Require C++23 for all targets that consume ThreadPoolPro.
target_compile_features(
    ThreadPoolPro
    INTERFACE
    cxx_std_23
)


# ============================================================================
# Tests
# ============================================================================

# Collect all test source files recursively.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

# Build the test executable.
add_executable(
    tests
    ${TEST_SOURCES}
)

# Allow simple includes like: #include "test_helper.h"
target_include_directories(
    tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Link ThreadPoolPro to the test executable.
target_link_libraries(
    tests
    PRIVATE
    ThreadPoolPro
)


# ============================================================================
# Benchmarks
# ============================================================================

# Collect all bench source files.
file(GLOB BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp"
)

# Build the bench executable
add_executable(
    benchmarks
    ${BENCH_SOURCES}
)

# Link ThreadPoolPro to the benchmarks executable
target_link_libraries(
    benchmarks
    PRIVATE
    ThreadPoolPro
)


# ============================================================================
# Examples
# ============================================================================

# Build every example in the examples/ directory.
file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
)

foreach(example ${EXAMPLE_SOURCES})
    get_filename_component(target "${example}" NAME_WE)

    add_executable(${target} "${example}")

    target_link_libraries(
        ${target}
        PRIVATE
        ThreadPoolPro
    )
endforeach()


# ============================================================================
# Installation
# ============================================================================

# Install the ThreadPoolPro target.
install(
    TARGETS ThreadPoolPro
    EXPORT ThreadPoolProTargets
)

# Install the exported CMake targets.
install(
    EXPORT ThreadPoolProTargets
    FILE ThreadPoolProTargets.cmake
    NAMESPACE ThreadPoolPro::
    DESTINATION lib/cmake/ThreadPoolPro
)

# Install the public header files.
install(
    DIRECTORY include/
    DESTINATION include
)


# ============================================================================
# Packaging
# ============================================================================

include(CMakePackageConfigHelpers)

# Generate the package configuration file.
configure_package_config_file(
    cmake/ThreadPoolProConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolProConfig.cmake
    INSTALL_DESTINATION lib/cmake/ThreadPoolPro
)

# Generate the package file.
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolProConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install the package configuration files.
install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolProConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolProConfigVersion.cmake
    DESTINATION lib/cmake/ThreadPoolPro
)