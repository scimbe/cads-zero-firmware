/*
 * CaDS Zero - ESP32Marauder CLI bridge, the WiFi recon/attack co-processor's
 * GUI face.
 *
 * Same "one selector + one shared tool view" shape as apps/active (see that
 * module's own header for the RAM reasoning - a cads_view_t is ~52 B and
 * this firmware's margin does not forgive one per tool). Tools split into
 * two kinds:
 *
 *   PASSIVE (Scan, Stop, List, Sniff BT) - send the command immediately on
 *   selection, no gate. They observe; they never put a frame on the air
 *   that wasn't already there.
 *
 *   ACTIVE (Deauth, Evil Portal, Beacon Spam, Probe Flood, BLE Spam) -
 *   transmit-based. Selecting one always lands in a CONFIRM mode first: an
 *   explicit warning that this sends real 802.11/BLE traffic and is for a
 *   network/area you control or are authorized to test, Yes/No, Back
 *   cancels. Only Yes starts it. This is not optional per-tool
 *   configuration - every active tool goes through the same gate, the same
 *   way apps/active's M9 suite already requires for its own transmit-based
 *   tools.
 *
 * The wire this bridges (CN8 pins 8/9, USART6, 115200 baud - see
 * docs/reference/marauder-coprocessor.md) carries Marauder's plaintext CLI:
 * a command line out, one or more response lines back. This module owns
 * that link exclusively while any of its views are open - it is the only
 * caller of cads_hal_wifi_uart_write/read while active. modules/wifi's
 * PPPoS path (a different protocol for a different, currently unwired,
 * co-processor) is not linked into the default build for exactly this
 * reason - see apps/settings/cads_settings.c's own note on that decision.
 *
 * BLUETOOTH TOOLS ARE HIDDEN UNTIL THE ESP32 IS ACTUALLY SEEN TALKING
 * ---------------------------------------------------------------------
 * There is no electrical presence-detect line on CN8 (just TX/RX), so
 * "is the co-processor there" can only ever be a software liveness check,
 * not a hardware fact. Every time the selector view is (re)entered - a
 * fresh open from the main menu, or popping back to it from a tool - it
 * sends a `stopscan` (2026-08-28 note: this doubles as the fix for a real
 * Marauder-firmware gotcha found the same day - see
 * docs/reference/marauder-coprocessor.md and this file's own cads_marauder.c
 * for the `wifi_scan_obj.scanning()` gate it clears) and starts a short
 * timer. Any bytes back before the timer expires mean something is alive
 * and talking on the wire, so the Bluetooth items get added to the
 * selector's menu (cads_menu_set_items() - the menu widget's own "contents
 * built at run time" mechanism, nothing bespoke here); no reply in time
 * means they get removed again. This is necessarily a snapshot from the
 * last time the menu was opened, not a continuous background poll - the
 * link is not chatty enough (and the RAM/CPU budget not generous enough)
 * to justify polling it on a timer while the user is looking at something
 * else entirely.
 */
#ifndef CADS_MARAUDER_H
#define CADS_MARAUDER_H

#include <stdbool.h>
#include <stdint.h>

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_MARAUDER      0x0C00u /**< selector: list of tools       */
#define CADS_VIEW_ID_MARAUDER_TOOL 0x0C01u /**< shared tool view              */

/* Reserved per-tool indices - never registered as views, just the stable
 * values the selector writes into the session block and the shared tool
 * view dispatches on (same pattern as apps/active's CADS_ACTIVE_TOOL_*). */
#define CADS_MARAUDER_TOOL_SCAN        0x0C02u
#define CADS_MARAUDER_TOOL_STOP        0x0C03u
#define CADS_MARAUDER_TOOL_LIST        0x0C04u
#define CADS_MARAUDER_TOOL_DEAUTH      0x0C05u
#define CADS_MARAUDER_TOOL_EVILPORTAL  0x0C06u
#define CADS_MARAUDER_TOOL_BEACON      0x0C07u
#define CADS_MARAUDER_TOOL_PROBE       0x0C08u
#define CADS_MARAUDER_TOOL_PCAP        0x0C09u
#define CADS_MARAUDER_TOOL_SNIFFBT     0x0C0Au /**< passive, hidden unless the ESP32 was just seen */
#define CADS_MARAUDER_TOOL_BLESPAM     0x0C0Bu /**< ACTIVE, hidden unless the ESP32 was just seen  */

/** Register the suite's two views. Call once from cads_menu_app_init()'s own
 *  init chain, the same way every other optional app does. No-op when
 *  `dispatcher` is NULL. */
void cads_marauder_init(cads_view_dispatcher_t* dispatcher);

/** Drive the output line reader and, while an active tool is running, its
 *  own re-send/keepalive timing. Call from the app-tree main loop
 *  (apps/bringup/explorer_app_demo.c's `d` loop), unconditionally - it is a
 *  cheap no-op (one UART ring drain) whenever the tool view is not open. */
void cads_marauder_tick(uint32_t now_ms);

/** True whenever this module is actively driving the WiFi UART link (a
 *  command was just sent, or a running tool's session is open) - the signal
 *  the "light organ" (apps/bringup's adapter-output VU animation) watches
 *  to know when to animate OUT0..15. False the instant the link goes idle. */
bool cads_marauder_link_active(void);

/**
 * Join `ssid`/`password` via a scan-and-match background search: starts a
 * `scanall`, watches every AP-format output line Marauder prints (via
 * cads_marauder_join.h's state machine) for one whose ESSID matches `ssid`,
 * counts its position among AP lines to find its access_points list index,
 * then sends `stopscan` followed by `join -a <index> -p <password>` -
 * Marauder's `join -a` only ever indexes into that scanned list (confirmed
 * against the pinned ESP32Marauder source; see cads_marauder_join.h for the
 * full reasoning on why this indirection is unavoidable). Runs in the
 * background independent of which tool view is open or gets navigated to
 * while it searches (cads_marauder_select() re-arms the line observer after
 * every reader reset for exactly this reason). Gives up quietly after
 * CADS_MARAUDER_JOIN_SCAN_TIMEOUT_MS if the SSID never appears - there is no
 * separate error-reporting channel today; a failed join is currently only
 * visible by its absence (no join attempt ever gets sent). Fire-and-forget,
 * intended to be called once from Settings when wifi.enabled and a
 * non-empty SSID are configured (see docs/reference/config-file.md's
 * wifi.* keys) - the config fields Marauder now uses, not the deferred PPP
 * path they were first added for.
 */
void cads_marauder_join(const char* ssid, const char* password);

/**
 * Set (or clear, with 0) the destination for the "Sniff (PCAP)" tool's live
 * relay: every 802.11 frame Marauder streams back over a `sniffraw
 * -serial` capture (demuxed from the same UART's ordinary CLI text by
 * cads_marauder_pcap.h - see that header for the wire-format reasoning) is
 * TZSP-encapsulated and sent as one UDP datagram to `ip_host`:
 * CADS_MARAUDER_PCAP_UDP_PORT via cads_net_udp_send(). `ip_host` is host
 * byte order, 0 = relay stays silent (frames are still parsed and counted,
 * just never sent - so the tool view's own frame counter still means
 * something with no target configured, e.g. while checking wiring before
 * pointing Wireshark at it). Intended to be called once from Settings when
 * cfg->pcap_target_ip changes, the same "only re-apply what changed"
 * pattern cads_settings_apply_config() already uses for net_ip and the
 * others - see that function's own comment.
 */
void cads_marauder_set_pcap_target(uint32_t ip_host);

#endif /* CADS_MARAUDER_H */
