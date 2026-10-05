#!/usr/bin/env bash
# CaDS Zero - build every lab variant the course material names.
#
# L08/L09 have the students rebuild the firmware with other TCP windows and a
# deeper Ethernet RX ring (CADS_RNLAB_TCP_MSS / _TCP_WND_MSS / _TCP_SND_BUF_MSS
# / CADS_RNLAB_ETH_RX_COUNT). Those builds sit close to the end of the SRAM,
# and nothing else builds them: CI's firmware job is the default
# configuration, the integration job one maximum-TCP build with the default
# ring. The mandatory L08 variant (1460/16, RX 32) stopped linking that way,
# unnoticed, when a later lesson's buffer landed in SRAM.
#
# Reads apps/rnlab/lab_variants.txt and, per line:
#   ok      configure + build (Debug, as the sheets say) in build/lab-variants,
#           then scripts/check_ram_budget.py; CMake's configure-time SRAM
#           estimate (top-level CMakeLists.txt) must not exceed what the
#           linker really placed by more than 256 B - an estimate that is too
#           high would reject combinations that fit;
#   reject  configure must fail with CMake's "does not fit" message.
#
# Usage: scripts/build_lab_variants.sh [variants-file]
# Exit 0 = every line behaved as expected.

source "$(dirname "${BASH_SOURCE[0]}")/cads_env.sh"
cads_require_arm_gcc

VARIANTS="${1:-${CADS_ROOT}/apps/rnlab/lab_variants.txt}"
BUILD_DIR="${CADS_ROOT}/build/lab-variants"
LOG="${BUILD_DIR}/variant.log"
SRAM_TOTAL=196608
mkdir -p "${BUILD_DIR}"

failed=0
printf '%-22s %-7s %10s %10s %9s  %s\n' "MSS/WND/SND_BUF/RX" "expect" "SRAM [B]" "reserve" "CCM [B]" "result"
while read -r mss wnd snd rx expect source; do
    case "${mss}" in ''|'#'*) continue ;; esac
    name="${mss}/${wnd}/${snd}/${rx}"
    result="ok"
    used="-"; reserve="-"; ccm="-"

    if cmake -S "${CADS_ROOT}" -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${CADS_ROOT}/cmake/arm-none-eabi-gcc.cmake" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCADS_RNLAB_TCP_MSS="${mss}" -DCADS_RNLAB_TCP_WND_MSS="${wnd}" \
        -DCADS_RNLAB_TCP_SND_BUF_MSS="${snd}" -DCADS_RNLAB_ETH_RX_COUNT="${rx}" \
        >"${LOG}" 2>&1; then
        configured=1
    else
        configured=0
    fi

    if [ "${expect}" = "reject" ]; then
        if [ "${configured}" = 1 ]; then
            result="FAIL: CMake accepted it"
        elif ! grep -q "does not fit the board's SRAM" "${LOG}"; then
            result="FAIL: configure failed without the SRAM message"
        else
            result="ok (rejected by CMake)"
        fi
    elif [ "${configured}" = 0 ]; then
        result="FAIL: configure"
    else
        estimate="$(sed -nE 's/.*SRAM estimate ([0-9]+) B.*/\1/p' "${LOG}" | tail -1)"
        if ! cmake --build "${BUILD_DIR}" >"${LOG}" 2>&1; then
            result="FAIL: build ($(grep -m1 -E 'will not fit|overflow|error' "${LOG}" | cut -c1-80))"
        else
            elf="${BUILD_DIR}/cads-zero.elf"
            reserve="$((16#$(arm-none-eabi-nm "${elf}" | awk '$3 == "__cads_heap_size" {print $1}')))"
            used="$((SRAM_TOTAL - reserve))"
            ccm="$(arm-none-eabi-size -A "${elf}" | awk '$1 == ".ccm" {print $2}')"
            if ! python3 "${CADS_ROOT}/scripts/check_ram_budget.py" "${elf}" >>"${LOG}" 2>&1; then
                result="FAIL: check_ram_budget.py"
            elif [ -z "${estimate}" ]; then
                result="FAIL: no SRAM estimate in the configure output"
            elif [ "$((estimate - used))" -gt 256 ]; then
                result="FAIL: CMake estimates ${estimate} B, ${used} B used"
            fi
        fi
    fi

    case "${result}" in FAIL*) failed=1; cp "${LOG}" "${BUILD_DIR}/failed-${mss}-${wnd}-${snd}-${rx}.log" ;; esac
    printf '%-22s %-7s %10s %10s %9s  %s\n' "${name}" "${expect}" "${used}" "${reserve}" "${ccm}" "${result}"
done <"${VARIANTS}"

if [ "${failed}" = 1 ]; then
    echo "At least one lab variant failed - logs: ${BUILD_DIR}/failed-*.log" >&2
    exit 1
fi
echo "All lab variants behave as expected."
