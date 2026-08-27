/*
 * CaDS Zero - ESP32Marauder CLI bridge, the WiFi recon/attack co-processor's
 * GUI face.
 *
 * Same "one selector + one shared tool view" shape as apps/active (see that
 * module's own header for the RAM reasoning - a cads_view_t is ~52 B and
 * this firmware's margin does not forgive one per tool). Tools split into
 * two kinds:
 *
 *   PASSIVE (Scan, Stop, List) - send the command immediately on selection,
 *   no gate. They observe; they never put a frame on the air that wasn't
 *   already there.
 *
 *   ACTIVE (Deauth, Evil Portal, Beacon Spam, Probe Flood) - transmit-based.
 *   Selecting one always lands in a CONFIRM mode first: an explicit warning
 *   that this sends real 802.11 traffic and is for a network you control or
 *   are authorized to test, Yes/No, Back cancels. Only Yes starts it. This
 *   is not optional per-tool configuration - every active tool goes through
 *   the same gate, the same way apps/active's M9 suite already requires for
 *   its own transmit-based tools.
 *
 * The wire this bridges (CN8 pins 8/9, USART6, 115200 baud - see
 * docs/reference/marauder-coprocessor.md) carries Marauder's plaintext CLI:
 * a command line out, one or more response lines back. This module owns
 * that link exclusively while any of its views are open - it is the only
 * caller of cads_hal_wifi_uart_write/read while active. modules/wifi's
 * PPPoS path (a different protocol for a different, currently unwired,
 * co-processor) is not linked into the default build for exactly this
 * reason - see apps/settings/cads_settings.c's own note on that decision.
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
 * Send `ssid`/`password` to Marauder's own join flow: adds the SSID as a
 * manual entry (`ssid -a -n <ssid>`) and joins it (`join -a <index> -p
 * <password>`), using the entry's own index rather than assuming 0 - see
 * cads_marauder.c's own comment on why. Fire-and-forget: the result (join
 * success/failure) shows up as ordinary output lines the tool view already
 * displays, the same as any other command. Intended to be called once from
 * Settings when wifi.enabled and a non-empty SSID are configured (see
 * docs/reference/config-file.md's wifi.* keys) - the config fields Marauder
 * now uses, not the deferred PPP path they were first added for.
 */
void cads_marauder_join(const char* ssid, const char* password);

#endif /* CADS_MARAUDER_H */
