/*
 * CaDS Zero - "Active Net Tools" (M9), the offensive/attack suite app.
 *
 * docs/ROADMAP.md's M9 track: seven tools that put forged frames on the wire
 * (ARP cache poisoning, rogue DHCP/DNS, 802.1X MAC cloning, VLAN hopping,
 * TCP RST injection, MQTT/CoAP reverse beacon, IPv6 RA flooding). Each is
 * gated by a dry-run mode (logs without TX) and an explicit confirm gate
 * before the first real packet, and each tool's help text states it is for
 * a controlled/isolated network only - the binding design minimums the
 * roadmap sets for this track.
 *
 * WHY ONE SUITE APP, NOT SEVEN
 * ---------------------------
 * The RAM margin is 256 B against the linker's 48 K floor and there is no
 * heap. A cads_view_t is ~52 B, so seven separate tool views (364 B) would
 * blow the margin on their own. The suite therefore registers exactly two
 * views: a selector (a cads_menu_t listing the seven tools) and ONE shared
 * tool view that renders and behaves per the recorded selection. Tool
 * state lives in a single shared session block (only one tool runs at a
 * time - the single-session-owner pattern apps/netiperf already uses),
 * not per-view.
 *
 * WHY THE CONFIRM GATE IS A MODE, NOT A cads_dialog_t
 * ---------------------------------------------------
 * cads_dialog_t is ~124 B static; the suite's whole budget is netiperf-
 * sized (~150 B). The confirm step is therefore a MODE inside the shared
 * tool view (a `confirm_pending` flag in the session block), not a
 * separate dialog struct: the same view draws a Yes/No prompt and handles
 * Ok=proceed / Back=cancel in its own input callback.
 *
 * VIEW IDS
 * --------
 * 0x0B00 selector, 0x0B01 shared tool view. 0x0B02..0x0B07 are reserved
 * #defines for the per-tool engines (the shared tool view records which
 * one is active in the session block; these ids are never registered, just
 * used as stable indices the menu's activate callback writes). The block
 * 0x0A00 belongs to apps/nettools and 0x0900 to apps/netiperf, so 0x0B00
 * is the next free range.
 */

#ifndef CADS_ACTIVE_H
#define CADS_ACTIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_ACTIVE      0x0B00u /**< selector: list of the 7 tools */
#define CADS_VIEW_ID_ACTIVE_TOOL 0x0B01u /**< shared tool view (renders per selection) */

/* Reserved per-tool indices - never registered as views, just the stable
 * values the selector writes into the session block's `selected_tool` and
 * the shared tool view dispatches on. Keeping them as named constants (not
 * bare 0..6) makes the switch in cads_active.c read as a tool list. */
#define CADS_ACTIVE_TOOL_ARP     0x0B02u /**< #1 gratuitous ARP cache poisoner */
#define CADS_ACTIVE_TOOL_DHCP     0x0B03u /**< #2 rogue DHCP + DNS sinkhole   */
#define CADS_ACTIVE_TOOL_8021X    0x0B04u /**< #3 802.1X supplicant bypass     */
#define CADS_ACTIVE_TOOL_VLANHOP  0x0B05u /**< #4 VLAN hopping injector        */
#define CADS_ACTIVE_TOOL_RSTD     0x0B06u /**< #5 VLAN hopping TCP RST daemon  */
#define CADS_ACTIVE_TOOL_BEACON   0x0B07u /**< #6 MQTT/CoAP reverse beacon    */
#define CADS_ACTIVE_TOOL_RA       0x0B08u /**< #7 IPv6 router advertisement    */

/** Register the suite's two views and seed the session block. Call once
 *  from cads_menu_app_init()'s own init chain, the same way every other
 *  optional app does. No-op when `dispatcher` is NULL. */
void cads_active_init(cads_view_dispatcher_t* dispatcher);

/**
 * Drive whichever tool engine is running. Call from the app-tree main loop
 * (apps/bringup/explorer_app_demo.c's `d` loop) alongside cads_game_tick()/
 * cads_nettools_tick() - a no-op unless a tool is actually in its run mode.
 */
void cads_active_tick(uint32_t now_ms);

/**
 * True while a promiscuous-capture tool (802.1X, TCP RST daemon) owns the
 * RX ring and has suppressed cads_net_poll(). The `d` loop gates
 * cads_net_poll() on `!cads_active_owns_rx()` so the two never fight over
 * the same RX descriptors. False for the TX-only and lwIP-RX tools, which
 * never take a capture session.
 */
bool cads_active_owns_rx(void);

#endif /* CADS_ACTIVE_H */