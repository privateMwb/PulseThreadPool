# ============================================================
# benchmarks/CMakeLists.txt
# Build configuration for the project benchmark suite.
# ============================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# oneTBB is the standard the benchmark suite compares this library
# against. find_package is preferred; on environments where it can't
# locate TBB's CMake config (e.g. some Termux/ARM package layouts),
# fall back to linking the library by name directly.
find_package(TBB QUIET)

# Collect all benchmark suite source files.
file(GLOB_RECURSE BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/suite/*.cpp"
)

# Build the benchmark executable.
add_executable(
    benchmarks
    ${BENCH_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench_main.cpp
)

# Allow simple benchmark includes.
target_include_directories(
    benchmarks
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

# Link the library under test, plus oneTBB, into the benchmark executable.
target_link_libraries(
    benchmarks
    PRIVATE
        ${PROJECT_NAME}
        $<IF:$<TARGET_EXISTS:TBB::tbb>,TBB::tbb,tbb>
)