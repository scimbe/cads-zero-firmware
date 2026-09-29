/*
 * CaDS Zero - passive L2 neighbor discovery (board side).
 *
 * WHY THIS DOES NOT CALL cads_net_poll() DURING CAPTURE
 * -------------------------------------------------------
 * Same reasoning as explorer_sniff_demo.c's own file header, not
 * re-derived here: cads_net_poll() drains the same RX descriptor ring
 * this file reads directly via cads_hal_eth_mac_receive(), and running
 * both at once would non-deterministically split frames between the two
 * consumers. Bring-up (the link wait below) still polls; the capture
 * loop itself has exclusive access.
 *
 * WHY THE RECEIVE BUFFER IS THE FULL FRAME SIZE
 * -------------------------------------------------
 * Same "unmeasured silent loss" trap explorer_sniff_demo.c's own header
 * documents: cads_hal_eth_mac_receive() drops a frame outright if it is
 * longer than the buffer it is given, and CDP/LLDP TLVs routinely run
 * past what a "just the header" buffer would hold.
 *
 * THE PARSING/DEDUP POLICY ITSELF LIVES IN cads/toolbox/l2discover.h
 * ---------------------------------------------------------------------
 * This file is only the board-specific capture loop and printout - the
 * actual CDP/LLDP/STP frame recognition and the dedup table are
 * portable and unit-tested on the host (tests/unit/test_l2discover.c)
 * with hand-built frames, proof the parsing is correct that does not
 * depend on this bench's own traffic (or lack of it, see
 * explorer_sniff_demo.c's own note on this bench's environment).
 *
 * VLAN IDs ARE TRACKED HERE, NOT IN THE TOOLBOX MODULE
 * ---------------------------------------------------------------------
 * "Distinct VLAN IDs seen" is a flat list with linear-scan dedup - three
 * lines, not a module. cads/toolbox/l2discover.h's own
 * cads_l2discover_vlan_tag() is the part actually worth unit-testing
 * (the 802.1Q TCI bit layout), and it is.
 */

#include "explorer_l2discover_demo.h"

#include <string.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/l2discover.h"
#include "cads_hal.h"
#include "explorer_capture_buffer.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

/* Small on purpose - each entry is a cads_l2discover_record_t, and this
 * board's SRAM has no room to spare (see targets/itsboard/linker/
 * cads_itsboard.ld's own ASSERT(__cads_heap_size >= 48K, ...), which this
 * exact command hit on its first build). A real segment rarely has more
 * than a handful of switches/APs actually announcing themselves. */
#define CADS_L2DISCOVER_DEMO_MAX_NEIGHBORS 6u
#define CADS_L2DISCOVER_DEMO_MAX_VLANS 8u

static const char* cads_l2discover_kind_text(cads_l2discover_kind_t kind) {
    switch(kind) {
    case CADS_L2_CDP: return "CDP ";
    case CADS_L2_LLDP: return "LLDP";
    case CADS_L2_STP: return "STP ";
    default: return "?   ";
    }
}

/** Adds `vlan_id` to `seen` if not already present and room remains.
 *  Returns the (possibly unchanged) count. Linear scan over at most
 *  CADS_L2DISCOVER_DEMO_MAX_VLANS entries - not worth a set structure. */
static uint32_t cads_l2discover_note_vlan(uint16_t* seen, uint32_t count, uint16_t vlan_id) {
    for(uint32_t i = 0u; i < count; i++) {
        if(seen[i] == vlan_id) return count;
    }
    if(count < CADS_L2DISCOVER_DEMO_MAX_VLANS) {
        seen[count] = vlan_id;
        return count + 1u;
    }
    return count;
}

void cads_explorer_l2discover_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 20u;

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * every other explorer_*_demo.c this session: this loop must call
     * cads_net_poll() itself to actually detect the link, not just check
     * its cached status. */
    (void)cads_explorer_net_link_wait(3000u);

    static cads_l2discover_record_t storage[CADS_L2DISCOVER_DEMO_MAX_NEIGHBORS];
    cads_l2discover_table_t table;
    cads_l2discover_table_init(&table, storage, CADS_L2DISCOVER_DEMO_MAX_NEIGHBORS);

    static uint16_t vlans_seen[CADS_L2DISCOVER_DEMO_MAX_VLANS];
    uint32_t vlan_count = 0u;

    cads_probe_puts("# l2discover: passive CDP/LLDP/STP/VLAN capture for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s (neighbor capacity=");
    cads_probe_put_uint(CADS_L2DISCOVER_DEMO_MAX_NEIGHBORS);
    cads_probe_puts(")\r\n");

    cads_hal_eth_mac_set_promiscuous(true);

    /* full receive size - see explorer_sniff_demo.c's file header on why;
     * shared, not this command's own buffer - see explorer_capture_buffer.h
     * on why (this was, in fact, the very buffer that first hit the floor). */
    uint8_t* frame = cads_explorer_capture_buffer();
    uint32_t frames_seen = 0u;
    uint32_t start = cads_hal_ticks_ms();

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint16_t length = cads_hal_eth_mac_receive(frame, CADS_EXPLORER_CAPTURE_BUFFER_SIZE);
        if(length == 0u) continue;
        frames_seen++;

        cads_l2discover_record_t record;
        if(cads_l2discover_parse(frame, length, &record)) {
            cads_l2discover_table_learn(&table, &record);
        }

        uint16_t vlan_id;
        if(cads_l2discover_vlan_tag(frame, length, &vlan_id)) {
            vlan_count = cads_l2discover_note_vlan(vlans_seen, vlan_count, vlan_id);
        }
    }

    cads_hal_eth_mac_set_promiscuous(false);

    cads_probe_puts("# l2discover: done, ");
    cads_probe_put_uint(frames_seen);
    cads_probe_puts(" frame(s) seen, ");
    cads_probe_put_uint((uint32_t)cads_l2discover_table_count(&table));
    cads_probe_puts(" neighbor(s), ");
    cads_probe_put_uint(cads_l2discover_table_dropped_total(&table));
    cads_probe_puts(" dropped (table full), ");
    cads_probe_put_uint(vlan_count);
    cads_probe_puts(" distinct VLAN ID(s)\r\n");

    for(size_t i = 0u; i < cads_l2discover_table_count(&table); i++) {
        const cads_l2discover_record_t* entry = cads_l2discover_table_at(&table, i);
        char mac_text[24];
        cads_fmt_mac(mac_text, sizeof(mac_text), entry->src_mac);

        cads_probe_puts("#   [");
        cads_probe_puts(cads_l2discover_kind_text(entry->kind));
        cads_probe_puts("] ");
        cads_probe_puts(mac_text);

        if(entry->kind == CADS_L2_STP) {
            bool is_root = (memcmp(entry->stp_root_mac, entry->stp_bridge_mac, 6u) == 0);
            char root_mac_text[24];
            cads_fmt_mac(root_mac_text, sizeof(root_mac_text), entry->stp_root_mac);
            char prio_text[8];
            cads_fmt_hex(prio_text, sizeof(prio_text), entry->stp_bridge_priority, 4u, true);
            cads_probe_puts("  bridge_prio=0x");
            cads_probe_puts(prio_text);
            cads_probe_puts(is_root ? "  ROOT BRIDGE" : "  root=");
            if(!is_root) cads_probe_puts(root_mac_text);
        } else {
            cads_probe_puts("  name=\"");
            cads_probe_puts(entry->name);
            cads_probe_puts("\"  port=\"");
            cads_probe_puts(entry->port);
            cads_probe_puts("\"");
        }
        cads_probe_puts("\r\n");
    }

    for(uint32_t i = 0u; i < vlan_count; i++) {
        cads_probe_puts("#   VLAN ");
        cads_probe_put_uint(vlans_seen[i]);
        cads_probe_puts(" tagged traffic observed\r\n");
    }
}
