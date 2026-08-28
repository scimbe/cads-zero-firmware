# ESP32Marauder co-processor: build and flash

An ESP32-WROOM-32 DevKit wired to the ITSboard's CN8 header (pins 8/9, PC6/PC7
= USART6) runs [ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder)
as a WiFi recon/attack co-processor. See
[wifi-coprocessor.md](wifi-coprocessor.md) for the wiring and the
(separate, deferred) PPPoS internet-connectivity path this same header could
instead carry.

**This page is about getting Marauder's own firmware onto that ESP32.**
None of it is CaDS Zero's own code - `tools/marauder-build/build_and_flash.sh`
manages a clone of the upstream project outside this repo and reproduces the
whole recipe in one command.

## Why this needed a script at all

Marauder ships prebuilt release binaries for every board target it supports
**except** `GENERIC_ESP32` - the plain, display-less WROOM-32 DevKit target
this project uses. Building it from source turned up:

- Six Arduino libraries `GENERIC_ESP32` needs that aren't vendored as git
  submodules of the Marauder repo itself (ESPAsyncWebServer, AsyncTCP,
  ESP32Ping, MicroNMEA, SoftwareSerial, and a specific NimBLE-Arduino tag).
- `GENERIC_ESP32`'s own board block in `configs.h` is missing `#define
  HAS_IDF_3`, which every *other* board target defines - an upstream gap,
  not a version mismatch on our end. Without it, `WiFiScan.cpp` falls back
  to ESP-IDF v3-era APIs (`tcpip_adapter_get_netif`, `esp_base_mac_addr_set`,
  `ESP_MAC_WIFI_STA`, ...) that no longer exist in the IDF 5.x a current
  arduino-esp32 core ships.
- A real link-time bug in `EvilPortal.h`: it *defines* (not declares)
  `index_html` for the non-PSRAM case, so every translation unit that
  includes it gets its own copy - "multiple definition" at link time. The
  PSRAM branch two lines below already does this correctly (`extern` in the
  header, one definition in `EvilPortal.cpp`); the fix just makes the
  non-PSRAM branch match it.
- Two mbedtls API renames in the vendored `ESPAsyncWebServer` library
  (`mbedtls_md5_starts_ret` etc. - the `_ret`-suffixed compat names mbedtls
  2.x had were dropped in mbedtls 3.x) and one ambiguous `IPAddress(0U)`
  overload against a newer core's constructor set.
- A linker refusal that is actually *load-bearing*: Marauder deliberately
  redefines `ieee80211_raw_frame_sanity_check` from the ESP-IDF's own
  `libnet80211.a` - the standard trick raw-injection projects use to get the
  stock WiFi stack to transmit frames it would otherwise reject (this is
  the actual mechanism behind deauth and other frame-injection attacks).
  Older toolchains silently let a user object file's symbol win over a
  static library's; the current one refuses unless told
  `-Wl,--allow-multiple-definition`.

None of this is a defect in *this* repository - it lives entirely in the
external Marauder clone the script manages - but rediscovering it cost real
time once, so the recipe is captured rather than left to be found again.

## Usage

```bash
tools/marauder-build/build_and_flash.sh                    # build + flash
tools/marauder-build/build_and_flash.sh --build-only        # build only
tools/marauder-build/build_and_flash.sh /dev/cu.usbserial-0001
```

The script installs `arduino-cli` and the pinned ESP32 core (3.3.4) via
Homebrew/`arduino-cli core install` if not already present, clones Marauder
at a pinned commit into `~/Documents/git/cads-zero-scratch/ESP32Marauder`
(override with `MARAUDER_CLONE_DIR`), fetches the missing libraries, applies
the four patches above (idempotently - safe to re-run), and compiles for
`GENERIC_ESP32`.

### Finding the port

The ESP32's USB-serial chip (CP2102/CH340, depending on the clone) shows up
as `/dev/cu.usbserial-*` on macOS - **not** `/dev/cu.usbmodem*`, which is the
ITSboard's ST-Link. If nothing shows up: the most common cause by far is a
charge-only USB cable (power lines wired, no D+/D- data lines) - a cable
that lights the board's power LED but produces literally no USB enumeration,
not even a driverless "unknown device" entry, is exactly this. Try a cable
you know moves data (one that has synced a phone, for instance).

### Bootloader mode

Cheap ESP32 DevKit clones frequently lack the auto-reset circuit that lets
`esptool` enter bootloader mode on its own. If upload fails with `Wrong boot
mode detected` or hangs on `Connecting...`: hold the board's **BOOT**
button, tap **EN**/RESET once while still holding BOOT, and keep holding
BOOT until the tool reports `Writing at 0x...`.

**2026-08-28 field note:** on a breadboard-mounted DevKit, a genuinely loose
header-to-breadboard contact produced three *different* esptool failure
messages across successive identical attempts (`Wrong boot mode detected
(0x13)`, `getting no sync reply: serial TX path seems to be down`, `No
serial data received`) - varying failure symptoms from otherwise identical
steps is itself the tell for a marginal physical connection, not a software
parameter to keep adjusting. Firmly reseating the board (straight down,
both header rows) resolved it immediately. Cable quality matters too - a
charge-only USB cable (power wires only, no D+/D-) produces zero USB
enumeration at all, not even a driverless "unknown device" entry; that's a
distinct symptom from this one and was ruled out separately before
reseating fixed it.

### Verified working (2026-08-28)

Built via this script, flashed to a genuine ESP-WROOM-32 DevKit
(MAC `24:0a:c4:62:1a:d8`), confirmed over the CLI at 115200 baud: boot
banner, `scanall` returns live AP/station scan results (SSID, BSSID, RSSI,
channel, AP-to-station associations) from the real RF environment, and
`stopscan` halts cleanly. WiFi capture/injection is genuinely working, not
just booting.

## Bluetooth (2026-08-28: fixed, flashed, hardware-verified - no touchscreen menu yet)

Bluetooth was disabled in earlier builds after enabling `HAS_BT` threw
roughly 15 NimBLE compile errors, on the assumption that Marauder's BLE
code called NimBLE 1.x-era methods (`getPayload()` returning `uint8_t*`,
`setAdvertisedDeviceCallbacks`, `setMinPreferred`/`setMaxPreferred`)
incompatible with the NimBLE-Arduino 2.3.8 this build pins. That diagnosis
was wrong about *why*: WiFiScan.cpp/.h already carry two complete,
independently correct implementations of every BLE call site, gated on a
`HAS_NIMBLE_2` macro every other real board target already defines
(`#ifndef HAS_NIMBLE_2` selects the 1.x-era calls above; `#else` selects
2.x's `NimBLEScanCallbacks`/`setScanCallbacks`/`getPayload()` returning
`std::vector<uint8_t>&`). `GENERIC_ESP32`'s own board block in `configs.h`
ships with `HAS_BT` on by default but `HAS_NIMBLE_2` off - an internally
inconsistent default once NimBLE-Arduino is pinned to 2.3.8, not a real
API mismatch needing porting work. `tools/marauder-build/build_and_flash.sh`
now defines both together; the previous "disable HAS_BT" workaround is
gone. No manual call-site porting was needed - confirmed by an actual
`arduino-cli compile` (not by reading the diff and assuming): clean build,
zero errors, first attempt, byte-identical output size across two
independent runs of the updated script from a pristine checkout.

**Flashed to real hardware and CLI-verified same day**, once the ESP32 was
physically connected via its own USB port (needed for the upload itself -
CN8's UART wiring alone isn't enough to flash new firmware) and someone was
at the board to hold BOOT during the upload (this DevKit clone has no
auto-reset circuit). `sniffbt` (bare, no `-t` filter - passive BLE scan)
returned real nearby devices over the raw serial link
(`board_cmd.py ~ <sec>`, temporarily pointed at `sniffbt` for this one
verification pass, then reverted - see that command's own comment in
`apps/bringup/explorer.c`) - MAC addresses, RSSI, and at least one
recognisable device name (`TUYA_...`) came back, proving the BLE stack
itself, not just the build, actually works.

**Not yet in `apps/marauder`'s touchscreen menu.** `apps/marauder/cads_marauder.c`'s
tool table (`cads_marauder_tools[]`) only has WiFi entries today - Scan
APs, Stop Scan, List APs, Deauth, Evil Portal, Beacon Spam, Probe Flood,
Sniff (PCAP). Adding Bluetooth tools (`sniffbt` as a passive entry;
`blespam` would need to go in as an ACTIVE one, behind the same mandatory
confirm dialog every other transmit-based tool already uses) is a real,
separate follow-up - not started, tracked as open work, not something this
verification pass silently included.

## The CLI itself

Marauder's serial CLI (what apps/marauder's CLI bridge on the STM32 side
drives - modules/wifi carries a different, deferred protocol for a possible
future second co-processor, see that module's own header) is documented at
[github.com/justcallmekoko/ESP32Marauder/wiki/cli](https://github.com/justcallmekoko/ESP32Marauder/wiki/cli)
and, more reliably, directly in the pinned commit's
`esp32_marauder/CommandLine.h` - the wiki page's detailed argument syntax
failed to render via automated fetch; the header's `HELP_*` string constants
are the authoritative, always-current source.
