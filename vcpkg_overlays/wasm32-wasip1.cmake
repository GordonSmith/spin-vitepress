set(VCPKG_ENV_PASSTHROUGH_UNTRACKED WASI_SDK_PREFIX PATH)

if(NOT DEFINED ENV{WASI_SDK_PREFIX})
    find_path(WASI_SDK_PREFIX
        "share/cmake/wasi-sdk-p1.cmake"
        HINTS "${CMAKE_CURRENT_LIST_DIR}/../wasi-sdk"
    )
else()
    set(WASI_SDK_PREFIX "$ENV{WASI_SDK_PREFIX}")
endif()

if(NOT EXISTS "${WASI_SDK_PREFIX}/share/cmake/wasi-sdk-p1.cmake")
    message(FATAL_ERROR "wasi-sdk-p1.cmake toolchain file not found; set WASI_SDK_PREFIX to the wasi-sdk root")
endif()

set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME WASI)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${WASI_SDK_PREFIX}/share/cmake/wasi-sdk-p1.cmake")

# The wasi-sdk toolchain ignores VCPKG_C(XX)_FLAGS, so seed the CMake cache instead.
set(_WASI_FLAGS "-fwasm-exceptions -D_WASI_EMULATED_MMAN")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_C_FLAGS=${_WASI_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${_WASI_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=-fwasm-exceptions -lwasi-emulated-mman"
)
