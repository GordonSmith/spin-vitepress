#!/usr/bin/env bash

set -euo pipefail

WASMTIME_VERSION=49.0.2
WASMTIME_SHA512=40edf2734f83ac74a69c8cd5ce7952248e95a4abc73ec1e40eee2c6e239d3a7624a44f94df2c1fffc28226db4f890ecfe0e87660b0d0f15fe5e0751eb3e7b0f3

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
INSTALL_DIR="${ROOT_DIR}/wasmtime"
ARCHIVE_NAME="wasmtime-v${WASMTIME_VERSION}-x86_64-linux.tar.xz"
DOWNLOAD_URL="https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/${ARCHIVE_NAME}"

if [[ $(uname -s) != "Linux" || $(uname -m) != "x86_64" ]]; then
    echo "Unsupported platform: $(uname -s) $(uname -m)" >&2
    exit 1
fi

if [[ -x "${INSTALL_DIR}/wasmtime" ]] && "${INSTALL_DIR}/wasmtime" --version | grep -q "${WASMTIME_VERSION}"; then
    echo "wasmtime ${WASMTIME_VERSION} is already installed in ${INSTALL_DIR}"
    exit 0
fi

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "${TEMP_DIR}"' EXIT

curl --fail --location --retry 3 --output "${TEMP_DIR}/${ARCHIVE_NAME}" "${DOWNLOAD_URL}"
echo "${WASMTIME_SHA512}  ${TEMP_DIR}/${ARCHIVE_NAME}" | sha512sum --check --status

rm -rf "${INSTALL_DIR}"
mkdir -p "${INSTALL_DIR}"
tar --extract --xz --file "${TEMP_DIR}/${ARCHIVE_NAME}" --directory "${INSTALL_DIR}" --strip-components=1

echo "Installed wasmtime ${WASMTIME_VERSION} in ${INSTALL_DIR}"
