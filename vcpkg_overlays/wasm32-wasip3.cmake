set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME WASI)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../wasi-sdk/share/cmake/wasi-sdk-p3.cmake")

# The wasi-sdk toolchain ignores VCPKG_C(XX)_FLAGS, so seed the CMake cache instead.
set(_WASI_FLAGS "-fwasm-exceptions -D_WASI_EMULATED_MMAN")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_C_FLAGS=${_WASI_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${_WASI_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=-fwasm-exceptions -lwasi-emulated-mman"
)
