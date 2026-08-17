#!/usr/bin/env bash
# Shared environment for the CaDS Zero build and flash scripts.
#
# The Arm toolchain lives inside the vcpkg artifact tree that the Keil Studio
# extension manages, so it is resolved rather than assumed to be on PATH.

set -euo pipefail

CADS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CADS_ROOT

_cads_newest() {
    # shellcheck disable=SC2012
    ls -d $1 2>/dev/null | sort -V | tail -1
}

CADS_GCC_BIN="${CADS_ARM_TOOLCHAIN_BIN:-$(_cads_newest "$HOME/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/*/bin")}"
CADS_CMAKE_BIN="$(_cads_newest "$HOME/.vcpkg/artifacts/*/tools.kitware.cmake/*/bin")"
CADS_NINJA_DIR="$(_cads_newest "$HOME/.vcpkg/artifacts/*/tools.ninja.build.ninja/*")"

if [ -n "${CADS_GCC_BIN}" ]; then export PATH="${CADS_GCC_BIN}:${PATH}"; fi
if [ -n "${CADS_CMAKE_BIN}" ]; then export PATH="${CADS_CMAKE_BIN}:${PATH}"; fi
if [ -n "${CADS_NINJA_DIR}" ]; then export PATH="${CADS_NINJA_DIR}:${PATH}"; fi

export CADS_ARM_TOOLCHAIN_BIN="${CADS_GCC_BIN}"

# The board this repository is developed against.
export CADS_STLINK_SERIAL="${CADS_STLINK_SERIAL:-066FFF565282494867161033}"
export CADS_CONSOLE_PORT="${CADS_CONSOLE_PORT:-}"

# Firmware occupies flash bank 1 only. Nothing in this repository ever writes
# below this address minus one, and nothing ever mass-erases. See docs/SAFETY.md.
export CADS_FLASH_BASE="0x08000000"
export CADS_FS_BASE="0x08120000"

cads_find_console_port() {
    if [ -n "${CADS_CONSOLE_PORT}" ]; then
        echo "${CADS_CONSOLE_PORT}"
        return 0
    fi
    # The ST-Link VCP enumerates as usbmodem<something>3 on macOS and
    # ttyACM<n> on Linux.
    for candidate in /dev/cu.usbmodem* /dev/ttyACM*; do
        [ -e "${candidate}" ] || continue
        echo "${candidate}"
        return 0
    done
    return 1
}
