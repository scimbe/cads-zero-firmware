#ifndef CADS_EXPLORER_SNIFF_DEMO_H
#define CADS_EXPLORER_SNIFF_DEMO_H

#include <stdint.h>

/**
 * Promiscuous-mode capture to `/sniff.pcap` (a real, minimal libpcap file -
 * LINKTYPE_ETHERNET, boot-relative timestamps since this board has no RTC)
 * for `seconds` (default 10). See explorer_sniff_demo.c's file header for
 * why this bypasses cads_net_poll() during the capture window and for how
 * loss is measured rather than assumed.
 *
 * Board only - promiscuous mode and the DMA descriptor ring are both real
 * hardware. explorer_sniff_demo_sim.c explains why the simulator does not
 * need its own version.
 */
void cads_explorer_sniff_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_SNIFF_DEMO_H */
