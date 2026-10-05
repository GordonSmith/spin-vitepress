cmake_minimum_required(VERSION 3.13)

set(VCPKG_ROOT ${CMAKE_SOURCE_DIR}/../vcpkg)
set(VCPKG_BUILD_ROOT ${CMAKE_SOURCE_DIR}/../build)
set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
set(VCPKG_INSTALLED_DIR "${VCPKG_BUILD_ROOT}/vcpkg_installed")
set(VCPKG_INSTALL_OPTIONS "--x-abi-tools-use-exact-versions;--downloads-root=${VCPKG_BUILD_ROOT}/vcpkg_downloads;--x-buildtrees-root=${VCPKG_BUILD_ROOT}/vcpkg_buildtrees;--x-packages-root=${VCPKG_BUILD_ROOT}/vcpkg_packages")
set(VCPKG_INCLUDE_DIR ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include)
set(VCPKG_OVERLAY_PORTS ${CMAKE_SOURCE_DIR}/../vcpkg-overlays)
set(VCPKG_OVERLAY_TRIPLETS ${CMAKE_SOURCE_DIR}/../vcpkg-overlays)

# try_compile runs from a scratch dir, so a relative chainload path would not resolve there.
if(VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    get_filename_component(_chainload "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_chainload}" CACHE FILEPATH "" FORCE)
endif()

project(HelloWasm VERSION 0.1.0)

find_package(DuckDB CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# set(WASI_SDK_DIR ../wasi-sdk)
# set(CMAKE_C_COMPILER ${WASI_SDK_DIR}/bin/clang)
# set(CMAKE_AR ${WASI_SDK_DIR}/bin/ar)
# set(CMAKE_C_FLAGS "-target wasm32-wasip2 -Wunused")

add_custom_command(
    OUTPUT 
        ${CMAKE_CURRENT_BINARY_DIR}/spin3_http_component_type.o
        ${CMAKE_CURRENT_BINARY_DIR}/spin3_http.c
    COMMAND wit-bindgen c --autodrop-borrows yes --world spin3-http ${CMAKE_CURRENT_SOURCE_DIR}/../test-ts/node_modules/@fermyon/spin-sdk/bin/wit
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../test-ts/node_modules/@fermyon/spin-sdk/bin/wit/world.wit
    COMMENT "Generating spin3_http_component_type.o"
)

add_executable(http_root_world.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/spin3_http.c
    ${CMAKE_CURRENT_BINARY_DIR}/spin3_http_component_type.o
)

target_link_libraries(http_root_world.wasm
    PRIVATE $<IF:$<TARGET_EXISTS:duckdb>,duckdb,duckdb_static>
    PRIVATE nlohmann_json::nlohmann_json
)

target_include_directories(http_root_world.wasm 
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE ${VCPKG_INCLUDE_DIR}
)
