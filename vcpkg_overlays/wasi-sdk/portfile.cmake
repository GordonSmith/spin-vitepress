# https://github.com/WebAssembly/wasi-sdk/releases

if(APPLE)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-${VERSION}-arm64-macos.tar.gz"
        FILENAME "wasi-sdk-${VERSION}-arm64-macos.tar.gz"
        SHA512 0
    )
elseif(UNIX)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-${VERSION}-x86_64-linux.tar.gz"
        FILENAME "wasi-sdk-${VERSION}-x86_64-linux.tar.gz"
        SHA512 12d773d5c3d4b333c82f3280ac753c93877966df84a11b70e70d07aa1dbf20607eb64ceae836e21f9c6aed44054dc3e50f910657bedc590415e3b02789ab4685
    )
elseif(WIN32)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-${VERSION}-x86_64-windows.tar.gz"
        FILENAME "wasi-sdk-${VERSION}-x86_64-windows.tar.gz"
        SHA512 0
    )
endif()

vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE ${ARCHIVE}
)

file(COPY ${SOURCE_PATH}/. DESTINATION ${CURRENT_PACKAGES_DIR}/wasi-sdk)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/wasi-sdk/share/wasi-sysroot/include/net" "${CURRENT_PACKAGES_DIR}/wasi-sdk/share/wasi-sysroot/include/scsi")

# Handle copyright
file(INSTALL ${SOURCE_PATH}/share/misc/config.guess DESTINATION ${CURRENT_PACKAGES_DIR}/share/wasi-sdk RENAME copyright)
