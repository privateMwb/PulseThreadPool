vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO privateMwb/LRUCache
    REF v1.0.0
    SHA512 a11df9f11d8bb99c8c912536a39031b69a7a3e4f76e0fb2b296e0608ef93565e9d3d1507ca71449021f2c7658f92f85fe68c5cc996598c6c216899d418fcb9bc
)

set(VCPKG_PORT_NAME ThreadPoolPro)

# Consumers only need the library itself, not the tests, benchmarks,
# regression tools, or examples. regression/ also fetches a third-party
# dependency via FetchContent at configure time, which requires network
# access that vcpkg's build sandbox does not allow.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DBUILD_BENCHMARKS=OFF
        -DBUILD_REGRESSION=OFF
        -DBUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME ${VCPKG_PORT_NAME}
    CONFIG_PATH lib/cmake/${VCPKG_PORT_NAME}
)

# This library is compiled (not header-only), so debug binaries are
# real and must be kept — only the duplicate debug/include headers
# are removed.
file(
    REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
)

vcpkg_install_copyright(
    FILE_LIST "${SOURCE_PATH}/LICENSE"
)