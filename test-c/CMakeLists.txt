cmake_minimum_required(VERSION 3.13)
project(HelloWasm VERSION 0.1.0)

# set(WASI_SDK_DIR ../vcpkg_installed/x64-linux/wasi-sdk)
# set(CMAKE_C_COMPILER ${WASI_SDK_DIR}/bin/clang)
# set(CMAKE_AR ${WASI_SDK_DIR}/bin/ar)
# set(CMAKE_C_FLAGS "-target wasm32-wasip2 -Wunused")

add_custom_command(
    OUTPUT 
        ${CMAKE_CURRENT_BINARY_DIR}/root_component_type.o
        ${CMAKE_CURRENT_BINARY_DIR}/root.c
    COMMAND wit-bindgen c --autodrop-borrows yes ${CMAKE_CURRENT_SOURCE_DIR}/wit
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wit/world.wit
    COMMENT "Generating root_component_type.o"
)

add_executable(http_root_world.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/root.c
    ${CMAKE_CURRENT_BINARY_DIR}/root_component_type.o
)

target_include_directories(http_root_world.wasm 
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
)
