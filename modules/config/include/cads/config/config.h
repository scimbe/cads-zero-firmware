/*
 * CaDS Zero - persistent, human-editable configuration.
 *
 * A single text file, `/config.txt`, in the littlefs volume (flash bank 2,
 * 0x08120000). The firmware reads it at boot; if it is missing it writes the
 * built-in defaults (the "base version") so the file always exists to edit.
 * Format is flat `key = value` lines with `#` comments - deliberately trivial
 * to hand-edit from a host that has pulled the file off the board's
 * filesystem (see scripts/cads_config.py). A Settings menu entry re-reads and
 * re-applies it without a reboot.
 *
 * The parse/serialize/defaults half is pure and portable (no storage, no HAL),
 * unit-tested on the host; cads_config_load/save wrap it over cads/storage.
 * Applying a config to the live subsystems (backlight, SPI clock, network)
 * is a firmware concern and lives in the app layer, not here.
 *
 * WiFi fields are carried now though the ESP32 dev-board UART driver is still
 * to come: they persist and round-trip so the config file is ready the day the
 * hardware lands. See docs and the project's WiFi memory.
 */

#ifndef CADS_CONFIG_H
#define CADS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CADS_CONFIG_PATH        "/config.txt"
#define CADS_CONFIG_SSID_MAX    33u  /* 32 + NUL */
#define CADS_CONFIG_PASS_MAX    64u  /* 63 + NUL */
#define CADS_CONFIG_UART_MAX    12u  /* "usart6"/"uart4" + slack */
/* load/save use this as the I/O bound - must be >= the longest a fully
 * populated config can actually serialize to. Found too small once already
 * (2026-08-29, adding net.mac_random/active.armed): the true worst case -
 * every field at its default plus a max-length wifi.ssid (32) and
 * wifi.password (63) - serializes to ~676 bytes, well past the old 512,
 * which cads_str_append's own bounded writes would have silently
 * truncated (dropping whatever came after, e.g. wifi.uart/wifi.pcap_target)
 * rather than erroring - caught by test_serialize_round_trips failing, not
 * by inspection. Recompute this worst case by hand before trusting any
 * future headroom claim here; do not just bump it and hope. */
#define CADS_CONFIG_TEXT_MAX    768u

typedef struct {
    bool boot_autostart;  /**< boot straight into the menu (console key exits) */
    uint8_t brightness;   /**< display backlight percent, 0..100            */
    bool fast_clock;      /**< display SPI fast divider                     */

    bool net_dhcp;        /**< DHCP vs the static fields below              */
    uint32_t net_ip;      /**< host byte order                             */
    uint32_t net_netmask; /**< host byte order                             */
    uint32_t net_gateway; /**< host byte order                             */
    /** Fresh random, locally-administered MAC every boot (via the hardware
     *  RNG - board-only, see cads_hal_rng_bytes()) instead of the fixed
     *  firmware default. OPSEC: a field device that always presents the
     *  same MAC on every engagement's network is a consistent, trackable
     *  identity across visits; off by default so debugging/ARP-table
     *  workflows that assume a stable address keep working unless this is
     *  deliberately turned on. See apps/bringup/explorer_eth.c. */
    bool net_mac_random;

    /** Device-wide safety catch for every transmit-based tool (Marauder's
     *  Deauth/Evil Portal/Beacon Spam/Probe Flood/BLE Spam, M9 Active Net
     *  Tools' forged-frame suite): `false` (the default) blocks the
     *  CONFIRM step's Yes from actually starting anything, however many
     *  times someone taps through the menu - only a deliberate config
     *  change (out of band from the touchscreen) arms it. A field
     *  engagement's scope boundary is set once here for the whole device,
     *  not per tool and not re-confirmable by a stray button sequence.
     *  Independent of M9's own existing per-session dry_run toggle (still
     *  live/session-only, still useful as a quick in-menu nudge) - this is
     *  the outer gate, that is the inner one. See apps/marauder/
     *  cads_marauder.c and apps/active/cads_active.c. */
    bool active_armed;

    bool wifi_enabled;    /**< carried for the coming ESP32 dev board       */
    char wifi_ssid[CADS_CONFIG_SSID_MAX];
    char wifi_password[CADS_CONFIG_PASS_MAX];
    char wifi_uart[CADS_CONFIG_UART_MAX]; /**< which UART the ESP is on     */

    /** Live-capture UDP target: where apps/marauder's PCAP-over-TZSP relay
     *  (a `sniffraw -serial` capture streamed to Wireshark's udpdump extcap
     *  over this board's own Ethernet link - see
     *  docs/reference/marauder-pcap-stream.md) sends each captured 802.11
     *  frame. Host byte order, 0 = unset (the relay stays silent). No port
     *  field - the relay always uses CADS_MARAUDER_PCAP_UDP_PORT
     *  (apps/marauder/cads_marauder_pcap.h), matching the fixed port
     *  Wireshark's udpdump is configured to listen on. */
    uint32_t pcap_target_ip;
} cads_config_t;

/** Fill `cfg` with the built-in base version (the file written when none
 *  exists). Static network 192.168.33.99/24, gateway .1; backlight 80; wifi
 *  off. Matches the firmware's own historical defaults. */
void cads_config_defaults(cads_config_t* cfg);

/**
 * Parse `len` bytes of `key = value` text into `cfg`. `cfg` MUST already hold
 * a valid base (call cads_config_defaults first): unknown or malformed lines
 * are skipped, and any key the text omits keeps its incoming value, so a
 * partial or hand-truncated file degrades to "defaults plus whatever parsed"
 * rather than a garbage struct. Returns the number of recognised keys applied.
 */
size_t cads_config_parse(const char* text, size_t len, cads_config_t* cfg);

/**
 * Serialize `cfg` to `out` (NUL-terminated) as the canonical `key = value`
 * file, with section comments. Returns the string length (excluding the NUL),
 * or 0 if `size` is too small. Round-trips exactly through cads_config_parse.
 */
size_t cads_config_serialize(const cads_config_t* cfg, char* out, size_t size);

/**
 * Load /config.txt from cads/storage into `cfg`. If the file does not exist,
 * `cfg` is set to defaults and the file is created with them (the base
 * version), so a first boot leaves an editable file behind. Returns 0 on
 * success (including the created-default case), negative on a storage error.
 * Starts from defaults internally, so `cfg` need not be pre-filled.
 */
int cads_config_load(cads_config_t* cfg);

/** Write `cfg` to /config.txt (create/truncate). Returns 0 on success. */
int cads_config_save(const cads_config_t* cfg);

/** True if the two configs differ in any field (for change detection). */
bool cads_config_equal(const cads_config_t* a, const cads_config_t* b);

#ifdef __cplusplus
}
#endif

#endif /* CADS_CONFIG_H */
