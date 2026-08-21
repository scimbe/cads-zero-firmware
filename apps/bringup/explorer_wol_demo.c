/*
 * CaDS Zero - Wake-on-LAN magic-packet sender (board side).
 *
 * SENDING, NOT RECEIVING
 * -------------------------
 * "independent of the board's own WoL support" (docs/ROADMAP.md's own
 * wording for this bullet): the LAN8742A PHY has its own hardware WoL
 * detection, entirely unrelated to this file. This is the other
 * direction - this board asking some OTHER device on the segment to
 * wake up, not this board itself being woken.
 *
 * RAW ETHERNET, NOT UDP BROADCAST
 * -----------------------------------
 * A Wake-on-LAN magic packet is a fixed payload - a 6-byte 0xFF sync
 * stream followed by the target's own MAC address repeated 16 times -
 * that is conventionally carried either as a UDP broadcast (commonly to
 * port 9) or as a raw Ethernet frame with no IP/UDP headers at all
 * (EtherType 0x0842); both forms are part of the same widely
 * implemented convention and either is accepted by WoL-capable NICs.
 * This uses the raw-Ethernet form, sent straight through
 * cads_hal_eth_mac_transmit() exactly like explorer_pktgen_demo.c,
 * bypassing lwIP entirely - not a style choice but a hard requirement
 * on this bench: this whole session's own long-established finding (no
 * DHCP server, so netif_ip4_addr() never becomes non-zero) means
 * ip4_route() refuses to send *any* IP packet, unicast or broadcast
 * (ip4.c's own routing check) - a UDP-broadcast magic packet would
 * never actually leave this board here. Raw Ethernet needs a link, not
 * an IP address, so it has no such dependency.
 */

#include "explorer_wol_demo.h"

#include <string.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

/* Not IANA-registered for anything else in active use; the same value
 * every common WoL raw-Ethernet sender (etherwake and its relatives)
 * uses, which is what a receiving NIC's own WoL frame filter is built to
 * recognise. */
#define CADS_WOL_ETHERTYPE_HI 0x08u
#define CADS_WOL_ETHERTYPE_LO 0x42u

#define CADS_WOL_SYNC_LEN     6u
#define CADS_WOL_MAC_REPEATS  16u
#define CADS_WOL_FRAME_SIZE   (6u + 6u + 2u + CADS_WOL_SYNC_LEN + CADS_WOL_MAC_REPEATS * 6u)

void cads_explorer_wol_demo(const uint8_t target_mac[6]) {
    static const uint8_t zero_mac[6] = {0u, 0u, 0u, 0u, 0u, 0u};
    if(memcmp(target_mac, zero_mac, 6u) == 0) {
        cads_probe_puts("# wol: no target given, e.g. W 0011223344AA\r\n");
        return;
    }

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * every other explorer_*_demo.c this session: this loop must call
     * cads_net_poll() itself to actually detect the link, not just check
     * its cached status. */
    uint32_t link_wait_start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - link_wait_start < 3000u) {
        cads_net_poll();

        cads_net_status_t status;
        cads_net_status(&status);
        if(status.link_up) break;
        cads_hal_delay_ms(10u);
    }

    uint8_t frame[CADS_WOL_FRAME_SIZE];
    uint8_t* p = frame;
    memset(p, 0xFFu, 6u);
    p += 6u; /* destination: broadcast, so no dependency on the switch
              * already having a fresh table entry for a target that may
              * be fully powered off */
    memcpy(p, cads_explorer_net_mac(), 6u);
    p += 6u; /* source: this device */
    *p++ = CADS_WOL_ETHERTYPE_HI;
    *p++ = CADS_WOL_ETHERTYPE_LO;
    memset(p, 0xFFu, CADS_WOL_SYNC_LEN);
    p += CADS_WOL_SYNC_LEN;
    for(uint32_t i = 0u; i < CADS_WOL_MAC_REPEATS; i++) {
        memcpy(p, target_mac, 6u);
        p += 6u;
    }

    char mac_text[24];
    cads_fmt_mac(mac_text, sizeof(mac_text), target_mac);
    cads_probe_puts("# wol: sending magic packet for ");
    cads_probe_puts(mac_text);
    cads_probe_puts("\r\n");

    bool sent = cads_hal_eth_mac_transmit(frame, sizeof(frame));

    cads_probe_puts(sent ? "# wol: sent\r\n" : "# wol: transmit failed (TX ring full or link down)\r\n");
}
