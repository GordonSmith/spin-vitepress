#!/usr/bin/env bash

set -euo pipefail

WASI_SDK_RELEASE=34
WASI_SDK_VERSION=34.0
WASI_SDK_SHA512=12d773d5c3d4b333c82f3280ac753c93877966df84a11b70e70d07aa1dbf20607eb64ceae836e21f9c6aed44054dc3e50f910657bedc590415e3b02789ab4685

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
INSTALL_DIR="${ROOT_DIR}/wasi-sdk"
ARCHIVE_NAME="wasi-sdk-${WASI_SDK_VERSION}-x86_64-linux.tar.gz"
DOWNLOAD_URL="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_RELEASE}/${ARCHIVE_NAME}"

if [[ $(uname -s) != "Linux" || $(uname -m) != "x86_64" ]]; then
    echo "Unsupported platform: $(uname -s) $(uname -m)" >&2
    exit 1
fi

if [[ -x "${INSTALL_DIR}/bin/clang" && -f "${INSTALL_DIR}/share/cmake/wasi-sdk-p1.cmake" ]]; then
    echo "wasi-sdk ${WASI_SDK_VERSION} is already installed in ${INSTALL_DIR}"
    exit 0
fi

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "${TEMP_DIR}"' EXIT

curl --fail --location --retry 3 --output "${TEMP_DIR}/${ARCHIVE_NAME}" "${DOWNLOAD_URL}"
echo "${WASI_SDK_SHA512}  ${TEMP_DIR}/${ARCHIVE_NAME}" | sha512sum --check --status

rm -rf "${INSTALL_DIR}"
mkdir -p "${INSTALL_DIR}"
tar --extract --gzip --file "${TEMP_DIR}/${ARCHIVE_NAME}" --directory "${INSTALL_DIR}" --strip-components=1

echo "Installed wasi-sdk ${WASI_SDK_VERSION} in ${INSTALL_DIR}"