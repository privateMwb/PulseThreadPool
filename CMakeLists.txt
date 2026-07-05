# ============================================================
# CMakeLists.txt
# Build configuration for the ThreadPoolPro library.
# ============================================================
#
# Sections:
#   1. C++ Configuration
#   2. ThreadPoolPro Library
#   3. Tests
#   4. Benchmarks
#   5. Examples
#   6. Installation
#   7. Packaging
#
# ============================================================

cmake_minimum_required(VERSION 3.20)

project(
    ThreadPoolPro
    VERSION 1.0.0
    LANGUAGES CXX
)

# ============================================================
# Section 1 — C++ Configuration
# ============================================================

# Configure the C++ language standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Section 2 — ThreadPoolPro Library
# ============================================================

# Collect all library source files.
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Build the ThreadPoolPro library.
add_library(
    ThreadPoolPro
    ${LIB_SOURCES}
)

# Expose the public include directory to consumers.
target_include_directories(
    ThreadPoolPro
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Require C++23 for all targets that consume ThreadPoolPro.
target_compile_features(
    ThreadPoolPro
    PUBLIC
    cxx_std_23
)

# ============================================================
# Section 3 — Tests
# ============================================================

# Collect all test source files.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

# Build the test executable.
add_executable(
    tests
    ${TEST_SOURCES}
)

# Allow simple test includes.
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

# ============================================================
# Section 4 — Benchmarks
# ============================================================

# Collect all benchmark source files.
file(GLOB_RECURSE BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp"
)

# Build the benchmark executable.
add_executable(
    benchmarks
    ${BENCH_SOURCES}
)

# Allow simple benchmark includes.
target_include_directories(
    benchmarks
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

# Link ThreadPoolPro to the benchmark executable.
target_link_libraries(
    benchmarks
    PRIVATE
    ThreadPoolPro
)

# ============================================================
# Section 5 — Examples
# ============================================================

# Collect all example source files.
file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
)

# Build the example executable.
add_executable(
    examples
    ${EXAMPLE_SOURCES}
)

# Allow simple example includes.
target_include_directories(
    examples
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples
)

# Link ThreadPoolPro to the example executable.
target_link_libraries(
    examples
    PRIVATE
    ThreadPoolPro
)

# ============================================================
# Section 6 — Installation
# ============================================================

# Install the ThreadPoolPro target and export it for downstream projects.
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

# ============================================================
# Section 7 — Packaging
# ============================================================

include(CMakePackageConfigHelpers)

# Generate the package configuration file.
configure_package_config_file(
    cmake/ThreadPoolProConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolProConfig.cmake
    INSTALL_DESTINATION lib/cmake/ThreadPoolPro
)

# Generate the package version file.
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