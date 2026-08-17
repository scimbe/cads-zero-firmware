#!/usr/bin/env bash
# Flash the CaDS Zero firmware to the attached ITSboard over ST-Link.
#
# SAFETY (docs/SAFETY.md):
#   - writes to 0x08000000 only, which st-flash sector-erases as needed;
#   - never issues a chip/mass erase, so the littlefs volume in flash bank 2
#     at 0x08120000 survives every firmware update;
#   - never touches option bytes, so read protection and the boot
#     configuration stay exactly as they are.

source "$(dirname "${BASH_SOURCE[0]}")/cads_env.sh"

BIN="${1:-${CADS_ROOT}/build/itsboard/cads-zero.bin}"

if [ ! -f "${BIN}" ]; then
    echo "error: ${BIN} not found - run scripts/build.sh first" >&2
    exit 1
fi

SIZE=$(wc -c < "${BIN}" | tr -d ' ')
LIMIT=$((1024 * 1024))
if [ "${SIZE}" -gt "${LIMIT}" ]; then
    echo "error: image is ${SIZE} bytes, which would run past flash bank 1 into the" >&2
    echo "       filesystem window at ${CADS_FS_BASE}. Refusing to flash." >&2
    exit 1
fi

echo "Flashing ${BIN} (${SIZE} bytes) to ${CADS_FLASH_BASE}"
st-flash --serial "${CADS_STLINK_SERIAL}" --reset write "${BIN}" "${CADS_FLASH_BASE}"
echo "Done."
