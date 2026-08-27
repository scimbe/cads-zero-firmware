#!/usr/bin/env bash
#
# CaDS Zero - build and flash ESP32Marauder for the GENERIC_ESP32 board
# target (a plain ESP32-WROOM-32 DevKit, no display - the WiFi/Marauder
# co-processor wired to CN8 pins 8/9, see docs/reference/wifi-coprocessor.md).
#
# This exists because getting from "git clone ESP32Marauder" to a flashable
# .bin took real archaeology on 2026-08-27/28: GENERIC_ESP32 has no CI-built
# release binary at all (checked github.com/justcallmekoko/ESP32Marauder's
# release assets - every board but this one ships one), six Arduino
# libraries it needs aren't vendored as git submodules, and the vendored
# source itself has three real bugs against a modern (2026) arduino-esp32
# core. None of this is CaDS Zero's own code - it lives entirely in a
# separate clone this script manages - but the recipe to get there is worth
# keeping so the next co-processor (or a fresh Mac) doesn't require
# rediscovering it from scratch.
#
# Usage:
#   tools/marauder-build/build_and_flash.sh [--build-only] [PORT]
#   PORT defaults to /dev/cu.usbserial-0001 (the CP2102/CH340 USB-serial
#   chip's port - NOT the ST-Link's /dev/cu.usbmodem*). Find it with:
#   ls /dev/cu.* before/after plugging the ESP32's own USB in.
#
# Prerequisite the script cannot do for you: put the ESP32 in bootloader
# mode before the upload step - hold BOOT, tap EN/RESET, keep holding BOOT
# through the "Connecting..." phase. Cheap ESP32 DevKit clones often lack
# the auto-reset circuit that would make this automatic. esptool retries on
# its own; hold BOOT until you see "Writing at 0x...".
set -euo pipefail

MARAUDER_COMMIT="91724fd8e964cb6f55cd7da60a3320570919de5b"  # pinned 2026-08-28; bump deliberately, re-verify patches still apply
CLONE_DIR="${MARAUDER_CLONE_DIR:-$HOME/Documents/git/cads-zero-scratch/ESP32Marauder}"
ARDUINO_LIBS="$HOME/Documents/Arduino/libraries"
FQBN="esp32:esp32:d32:PartitionScheme=min_spiffs"
ESP32_CORE_VERSION="3.3.4"
BUILD_DIR="/tmp/marauder_build"

PORT="/dev/cu.usbserial-0001"
BUILD_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --build-only) BUILD_ONLY=1 ;;
    /dev/*) PORT="$arg" ;;
  esac
done

export PATH="$PATH:$HOME/Library/Python/3.14/bin"  # platformio isn't actually used by this script (arduino-cli does the real work) - not added to PATH

echo "== toolchain =="
command -v arduino-cli >/dev/null || { echo "installing arduino-cli..."; brew install arduino-cli; }
if [ ! -f "$HOME/Library/Arduino15/arduino-cli.yaml" ]; then
  arduino-cli config init
fi
arduino-cli config set board_manager.additional_urls \
  "https://github.com/espressif/arduino-esp32/releases/download/${ESP32_CORE_VERSION}/package_esp32_dev_index.json"
arduino-cli core update-index
arduino-cli core install "esp32:esp32@${ESP32_CORE_VERSION}"

echo "== ESP32Marauder source (pinned commit) =="
mkdir -p "$(dirname "$CLONE_DIR")"
if [ ! -d "$CLONE_DIR/.git" ]; then
  git clone https://github.com/justcallmekoko/ESP32Marauder.git "$CLONE_DIR"
fi
cd "$CLONE_DIR"
git fetch origin "$MARAUDER_COMMIT" 2>/dev/null || true
git checkout "$MARAUDER_COMMIT" 2>&1 | tail -3
git submodule update --init --recursive

echo "== external libraries (not vendored by Marauder itself) =="
mkdir -p "$ARDUINO_LIBS"
clone_lib() {  # clone_lib <url> <dest-name> [ref]
  local url="$1" name="$2" ref="${3:-}"
  if [ -d "$ARDUINO_LIBS/$name/.git" ]; then return; fi
  if [ -n "$ref" ]; then
    git clone --branch "$ref" --depth 1 "$url" "$ARDUINO_LIBS/$name"
  else
    git clone --depth 1 "$url" "$ARDUINO_LIBS/$name"
  fi
}
clone_lib https://github.com/bigbrodude6119/ESPAsyncWebServer.git ESPAsyncWebServer
clone_lib https://github.com/me-no-dev/AsyncTCP.git AsyncTCP
clone_lib https://github.com/marian-craciunescu/ESP32Ping.git ESP32Ping
clone_lib https://github.com/stevemarple/MicroNMEA.git MicroNMEA
# The current (8.x) espsoftwareserial needs an extra "ghostl" dependency for
# circular_queue.h; 6.17.1 bundles circular_queue.h directly under
# src/circular_queue/ and needs nothing else - simpler, and all Marauder's
# GPS code (the only caller) needs is the basic RX/TX API both versions share.
clone_lib https://github.com/plerup/espsoftwareserial.git SoftwareSerial 6.17.1

echo "== NimBLE-Arduino version =="
# Pinned to match the CI recipe used for boards on this same core version
# (idf_ver 3.3.4 -> nimble_ver 2.3.8 in .github/workflows/nightly_build.yml's
# matrix). NOT currently exercised: HAS_BT is off for GENERIC_ESP32 below,
# because the vendored source's Bluetooth code (WiFiScan.cpp's BLE spam/scan
# paths) calls NimBLE 1.x-era methods (getPayload() returning uint8_t*,
# setAdvertisedDeviceCallbacks, getNative(), setMinPreferred/MaxPreferred)
# that don't exist on NimBLE 2.x. Re-enabling Bluetooth needs actually
# porting those call sites to the 2.x API (getPayload() now returns
# std::vector<uint8_t>, setAdvertisedDeviceCallbacks -> setScanCallbacks,
# etc.) - real work, not a version pin. Tracked as a follow-up; see the repo
# CLAUDE.md.
(cd esp32_marauder/libraries/NimBLE-Arduino && git fetch --tags && git checkout 2.3.8 2>&1 | tail -2)

echo "== source patches (against the pinned commit above) =="
cd esp32_marauder

# 1. GENERIC_ESP32 board target: select it, and disable Bluetooth (see the
#    NimBLE note above - this is the one deliberate feature reduction).
sed -i '' 's|//#define GENERIC_ESP32|#define GENERIC_ESP32|' configs.h

# 2. GENERIC_ESP32's own board block is missing HAS_IDF_3, which every
#    other board target in this file defines - without it, WiFiScan.cpp
#    falls back to legacy ESP-IDF v3-era APIs (tcpip_adapter_get_netif,
#    esp_base_mac_addr_set, ESP_MAC_WIFI_STA, esp_read_mac,
#    esp_event_send_internal, g_wifi_feature_caps, esp_spiram_init) that
#    plain don't exist in the IDF 5.x arduino-esp32 3.x ships. Upstream gap,
#    not a version issue - every real board target already has this line.
if ! grep -q 'ifdef GENERIC_ESP32$' configs.h; then
  echo "FATAL: configs.h layout changed, GENERIC_ESP32 block not found - re-check this script's patches" >&2
  exit 1
fi
python3 - "$PWD/configs.h" << 'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "#ifdef GENERIC_ESP32\n    //#define FLIPPER_ZERO_HAT\n    //#define HAS_BATTERY\n    #define HAS_BT\n"
new = "#ifdef GENERIC_ESP32\n    #define HAS_IDF_3\n    //#define FLIPPER_ZERO_HAT\n    //#define HAS_BATTERY\n    //#define HAS_BT\n"
if old in s:
    open(p, "w").write(s.replace(old, new))
elif "#define HAS_IDF_3" in s.split("#ifdef GENERIC_ESP32")[1].split("#endif")[0]:
    pass  # already patched (idempotent re-run)
else:
    sys.exit("configs.h GENERIC_ESP32 block text changed upstream - patch 2 needs updating by hand")
PYEOF

# 3. EvilPortal.h defines (not declares) `char index_html[MAX_HTML_SIZE]` for
#    the non-PSRAM case, so every .cpp that includes the header gets its own
#    copy -> "multiple definition of index_html" at link time. The PSRAM
#    branch right next to it already does this correctly (extern in the
#    header, one real definition in EvilPortal.cpp) - this makes the
#    non-PSRAM branch match that existing, working pattern.
if grep -q 'char index_html\[MAX_HTML_SIZE\] = "TEST";' EvilPortal.h; then
  sed -i '' 's|char index_html\[MAX_HTML_SIZE\] = "TEST";|extern char index_html[MAX_HTML_SIZE];|' EvilPortal.h
  python3 - "$PWD/EvilPortal.cpp" << 'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "#ifdef HAS_PSRAM\n  char* index_html = nullptr;\n#endif"
new = '#ifdef HAS_PSRAM\n  char* index_html = nullptr;\n#else\n  char index_html[MAX_HTML_SIZE] = "TEST";\n#endif'
assert old in s, "EvilPortal.cpp HAS_PSRAM block text changed upstream - patch 3 needs updating by hand"
open(p, "w").write(s.replace(old, new))
PYEOF
fi

# 4. Two mbedtls API changes in the vendored ESPAsyncWebServer library (not
#    Marauder's own code): mbedtls 3.x (shipped by this core) dropped the
#    _ret-suffixed compat names mbedtls 2.x had (mbedtls_md5_starts_ret etc
#    - the base names now return int directly, so this is a straight
#    rename), and IPAddress(0U) became an ambiguous overload against a
#    newer core's IPAddress constructor set (needs an explicit uint32_t
#    cast to pick one).
WEBSERVER_SRC="$ARDUINO_LIBS/ESPAsyncWebServer/src"
sed -i '' \
  -e 's/mbedtls_md5_starts_ret/mbedtls_md5_starts/g' \
  -e 's/mbedtls_md5_update_ret/mbedtls_md5_update/g' \
  -e 's/mbedtls_md5_finish_ret/mbedtls_md5_finish/g' \
  -e 's/mbedtls_sha1_starts_ret/mbedtls_sha1_starts/g' \
  -e 's/mbedtls_sha1_update_ret/mbedtls_sha1_update/g' \
  -e 's/mbedtls_sha1_finish_ret/mbedtls_sha1_finish/g' \
  "$WEBSERVER_SRC/WebAuthentication.cpp"
sed -i '' 's/return IPAddress(0U);/return IPAddress((uint32_t)0U);/' "$WEBSERVER_SRC/AsyncWebSocket.cpp"

echo "== linking libraries into the Arduino sketchbook =="
for d in libraries/*/; do
  name="$(basename "$d")"
  ln -sfn "$PWD/$d" "$ARDUINO_LIBS/$name"
done

echo "== compiling =="
arduino-cli compile \
  --fqbn "$FQBN" \
  --build-property "compiler.cpp.extra_flags=-DGENERIC_ESP32" \
  --build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition" \
  --warnings none \
  --output-dir "$BUILD_DIR" \
  esp32_marauder

echo "== build complete: $BUILD_DIR =="
if [ "$BUILD_ONLY" = "1" ]; then
  echo "--build-only requested, not flashing."
  exit 0
fi

echo "== flashing to $PORT =="
echo "If this hangs on 'Connecting...': hold the ESP32's BOOT button, tap"
echo "EN/RESET once, keep holding BOOT until you see 'Writing at 0x...'."
arduino-cli upload \
  --fqbn "$FQBN" \
  --port "$PORT" \
  --input-dir "$BUILD_DIR" \
  esp32_marauder

echo "== done =="
