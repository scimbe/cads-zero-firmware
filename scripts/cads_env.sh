#!/usr/bin/env bash
# Shared environment for the CaDS Zero build and flash scripts.
#
# The Arm toolchain is resolved rather than assumed, in this order:
#   1. CADS_ARM_TOOLCHAIN_BIN, if set (a directory holding arm-none-eabi-gcc)
#   2. the newest vcpkg artifact tree Keil Studio manages (macOS dev machines)
#   3. /opt/arm-gnu-toolchain/bin (the firmware-lab course image)
#   4. whatever arm-none-eabi-gcc is on PATH (Linux, Windows/Git Bash, CI)
# Every one of those is optional: the vcpkg tree only exists where Keil Studio
# is installed, and a missing one must not abort the script - which it did
# under `set -euo pipefail` (an `ls` with no match failing the pipeline) on
# every machine without it. Only scripts that really compile call
# cads_require_arm_gcc(), which then says clearly what is missing.

set -euo pipefail

CADS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CADS_ROOT

# Newest directory matching a glob, or nothing - never a failing pipeline.
_cads_newest() {
    local match
    # shellcheck disable=SC2012
    match="$(ls -d $1 2>/dev/null | sort -V | tail -1 || true)"
    echo "${match}"
}

_cads_find_gcc_bin() {
    local dir
    if [ -n "${CADS_ARM_TOOLCHAIN_BIN:-}" ]; then
        echo "${CADS_ARM_TOOLCHAIN_BIN}"
        return
    fi
    dir="$(_cads_newest "${HOME}/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/*/bin")"
    if [ -n "${dir}" ]; then
        echo "${dir}"
        return
    fi
    if [ -x /opt/arm-gnu-toolchain/bin/arm-none-eabi-gcc ]; then
        echo /opt/arm-gnu-toolchain/bin
        return
    fi
    if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        dirname "$(command -v arm-none-eabi-gcc)"
        return
    fi
    echo ""
}

CADS_GCC_BIN="$(_cads_find_gcc_bin)"
CADS_CMAKE_BIN="$(_cads_newest "${HOME}/.vcpkg/artifacts/*/tools.kitware.cmake/*/bin")"
CADS_NINJA_DIR="$(_cads_newest "${HOME}/.vcpkg/artifacts/*/tools.ninja.build.ninja/*")"

if [ -n "${CADS_GCC_BIN}" ]; then export PATH="${CADS_GCC_BIN}:${PATH}"; fi
if [ -n "${CADS_CMAKE_BIN}" ]; then export PATH="${CADS_CMAKE_BIN}:${PATH}"; fi
if [ -n "${CADS_NINJA_DIR}" ]; then export PATH="${CADS_NINJA_DIR}:${PATH}"; fi

export CADS_ARM_TOOLCHAIN_BIN="${CADS_GCC_BIN}"

# For scripts that compile firmware: stop with a clear message instead of a
# CMake error three screens later.
cads_require_arm_gcc() {
    if [ -z "${CADS_GCC_BIN}" ] || { [ ! -x "${CADS_GCC_BIN}/arm-none-eabi-gcc" ] &&
                                     [ ! -x "${CADS_GCC_BIN}/arm-none-eabi-gcc.exe" ]; }; then
        echo "error: no Arm GNU toolchain found (arm-none-eabi-gcc)." >&2
        echo "       Looked in: \$CADS_ARM_TOOLCHAIN_BIN, ~/.vcpkg (Keil Studio)," >&2
        echo "       /opt/arm-gnu-toolchain/bin and PATH." >&2
        echo "       Install the Arm GNU Toolchain and put its bin/ on PATH, or set" >&2
        echo "       CADS_ARM_TOOLCHAIN_BIN to that directory." >&2
        exit 1
    fi
    for tool in cmake ninja; do
        if ! command -v "${tool}" >/dev/null 2>&1; then
            echo "error: '${tool}' not found on PATH (needed to build)." >&2
            exit 1
        fi
    done
}

# Parallel jobs for `cmake --build`. In a container `nproc` reports the host's
# cores; ninja then starts about nproc+2 jobs, the container's CPU quota
# throttles them and an IDE in the same container stutters. Prints the job
# count that fits the container's quota, or NOTHING where there is no cgroup
# CPU file (macOS, Windows, a plain Linux host) - the caller then passes no -j
# and the build behaves as before.
#
#   jobs = ceil(quota / period), at least 1, at most the CPU count;
#   cgroup v2 /sys/fs/cgroup/cpu.max, else cgroup v1 cpu.cfs_quota_us /
#   cpu.cfs_period_us; "max", -1 or an unreadable value -> the CPU count.
#
# Inside the firmware-lab image `cads-jobs` does the same and is used instead.
# Tests point the files elsewhere: CADS_JOBS_CPU_MAX, CADS_JOBS_V1_QUOTA,
# CADS_JOBS_V1_PERIOD, CADS_JOBS_NPROC.
cads_build_jobs() {
    if command -v cads-jobs >/dev/null 2>&1; then
        cads-jobs
        return 0
    fi
    local v2="${CADS_JOBS_CPU_MAX:-/sys/fs/cgroup/cpu.max}"
    local v1q="${CADS_JOBS_V1_QUOTA:-/sys/fs/cgroup/cpu/cpu.cfs_quota_us}"
    local v1p="${CADS_JOBS_V1_PERIOD:-/sys/fs/cgroup/cpu/cpu.cfs_period_us}"
    local quota="" period="" extra="" cpus
    if [ -r "${v2}" ]; then
        read -r quota period extra < "${v2}" || true
    elif [ -r "${v1q}" ] && [ -r "${v1p}" ]; then
        read -r quota < "${v1q}" || true
        read -r period < "${v1p}" || true
    else
        return 0
    fi
    cpus="${CADS_JOBS_NPROC:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)}"
    [[ "${cpus}" =~ ^[1-9][0-9]*$ ]] || cpus=1
    if [ -z "${extra}" ] && [[ "${quota}" =~ ^[1-9][0-9]*$ ]] && [[ "${period}" =~ ^[1-9][0-9]*$ ]]; then
        local jobs=$(( (quota + period - 1) / period ))
        [ "${jobs}" -le "${cpus}" ] || jobs="${cpus}"
        echo "${jobs}"
    else
        echo "${cpus}"
    fi
}

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
