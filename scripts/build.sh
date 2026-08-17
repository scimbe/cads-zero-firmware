#!/usr/bin/env bash
# Build the CaDS Zero firmware for the ITSboard.
#
# Usage: scripts/build.sh [Debug|Release] [extra cmake args...]

source "$(dirname "${BASH_SOURCE[0]}")/cads_env.sh"

BUILD_TYPE="${1:-Debug}"
shift || true

BUILD_DIR="${CADS_ROOT}/build/itsboard"

cmake -S "${CADS_ROOT}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${CADS_ROOT}/cmake/arm-none-eabi-gcc.cmake" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    "$@"

cmake --build "${BUILD_DIR}"

echo
echo "Artifacts:"
ls -la "${BUILD_DIR}"/cads-zero.elf "${BUILD_DIR}"/cads-zero.bin "${BUILD_DIR}"/cads-zero.hex
