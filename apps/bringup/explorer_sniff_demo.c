/*
 * CaDS Zero - promiscuous packet sniffer (board side).
 *
 * WHY THIS DOES NOT CALL cads_net_poll() DURING CAPTURE
 * -------------------------------------------------------
 * cads_net_poll() drains the same RX descriptor ring this file reads
 * directly via cads_hal_eth_mac_receive() (cads_net_board.c's own
 * cads_net_receive_pump() calls the identical function). Running both at
 * once would non-deterministically split frames between this capture and
 * lwIP's own protocol processing - neither would see everything, and
 * "frames captured" would stop meaning what it says. Bring-up (the link
 * wait below) still polls; the capture loop itself has this driver's
 * exclusive attention until it ends. PA7 arbitration is unaffected either
 * way - hal_spi.c claims/releases it per display blit, not from
 * cads_net_poll(), and the MAC/DMA hardware keeps receiving on its own
 * regardless of who is draining it.
 *
 * MEASURING LOSS, NOT ASSUMING IT
 * ---------------------------------
 * docs/ROADMAP.md's own wording for this bullet: frame loss under load is
 * likely and must be measured. Three independent counters, each catching
 * a different failure mode:
 *   - cads_hal_eth_mac_missed_frames()'s `no_descriptor` result
 *     (ETH_DMAMFBOCR.MFC - see hal_eth_mac.h's own note on why this is
 *     NOT what its field name suggests): frames the DMA discarded because
 *     every RX descriptor was still full - this driver (or storage under
 *     it) not keeping up.
 *   - the same call's `fifo_overflow` result (DMAMFBOCR.MFA): frames lost
 *     to the MAC's own receive FIFO overflowing - a wire-level condition,
 *     not something this software could have prevented.
 *   - `write_errors` below: a captured frame that storage refused (full
 *     volume, I/O error) - counted separately from either hardware
 *     counter, since it happens after a frame was already, successfully,
 *     received.
 *
 * PCAP, TRUNCATED SNAPLEN VS FULL RECEIVE
 * ------------------------------------------
 * cads_hal_eth_mac_receive() drops (not truncates) a frame larger than
 * the buffer it is given - see that function's own "counted, not
 * truncated silently" contract. Passing it anything smaller than
 * CADS_ETH_BUF_SIZE (hal_eth_mac.c) would turn ordinary oversized frames
 * into unmeasured, silent loss, defeating the point of this file. So the
 * receive buffer is the full frame size; only the copy handed to pcap is
 * capped at CADS_SNIFF_SNAPLEN, the standard tcpdump-style "capture
 * everything, store only the first N bytes, but keep counting the true
 * length" - `orig_len` in the per-packet header is always the real
 * length, `incl_len` is what was actually written.
 */

#include "explorer_sniff_demo.h"

#include <string.h>

#include "cads/net/net.h"
#include "cads/storage/storage.h"
#include "cads_hal.h"
#include "explorer_capture_buffer.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

/* Standard pcap file magic for microsecond-resolution, native-endian
 * (this target is little-endian) timestamps, version 2.4, LINKTYPE_ETHERNET
 * (1). Timestamps are cads_hal_ticks_ms() split into seconds/microseconds -
 * boot-relative, not wall-clock (this board has no RTC), so an opened
 * capture shows dates near the 1970 epoch rather than the real capture
 * time. Documented here rather than hidden: every frame's relative timing
 * within one capture is still exact, which is what a loss/rate
 * measurement actually needs. */
#define CADS_SNIFF_SNAPLEN 256u

static void cads_sniff_put_u32le(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void cads_sniff_write_global_header(uint8_t* out) {
    cads_sniff_put_u32le(out + 0, 0xA1B2C3D4u); /* magic: microsecond timestamps, native order */
    out[4] = 0x02u;
    out[5] = 0x00u; /* version_major = 2 */
    out[6] = 0x04u;
    out[7] = 0x00u; /* version_minor = 4 */
    cads_sniff_put_u32le(out + 8, 0u);  /* thiszone: GMT */
    cads_sniff_put_u32le(out + 12, 0u); /* sigfigs: unused, always 0 */
    cads_sniff_put_u32le(out + 16, CADS_SNIFF_SNAPLEN);
    cads_sniff_put_u32le(out + 20, 1u); /* network: LINKTYPE_ETHERNET */
}

static void cads_sniff_write_packet_header(uint8_t* out, uint32_t now_ms, uint32_t incl_len, uint32_t orig_len) {
    cads_sniff_put_u32le(out + 0, now_ms / 1000u);
    cads_sniff_put_u32le(out + 4, (now_ms % 1000u) * 1000u);
    cads_sniff_put_u32le(out + 8, incl_len);
    cads_sniff_put_u32le(out + 12, orig_len);
}

void cads_explorer_sniff_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 10u;

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * explorer_arp_demo.c/explorer_ping_demo.c/explorer_traceroute_demo.c/
     * explorer_pktgen_demo.c: this loop must call cads_net_poll() itself
     * to actually detect the link, not just check its cached status. */
    (void)cads_explorer_net_link_wait(3000u);

    int mount = cads_storage_mount();
    if(mount == CADS_STORAGE_ERR_CORRUPT) {
        cads_probe_puts("# sniff: no filesystem found - formatting (this is the first run)\r\n");
        mount = cads_storage_format();
    }
    if(mount != CADS_STORAGE_OK) {
        cads_probe_puts("# sniff: storage unavailable: ");
        cads_probe_puts(cads_storage_status_text(mount));
        cads_probe_puts("\r\n");
        return;
    }

    cads_storage_file_t* file;
    int opened = cads_storage_open(
        &file, "/sniff.pcap", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC);
    if(opened != CADS_STORAGE_OK) {
        cads_probe_puts("# sniff: could not open /sniff.pcap: ");
        cads_probe_puts(cads_storage_status_text(opened));
        cads_probe_puts("\r\n");
        cads_storage_unmount();
        return;
    }

    uint8_t header[24];
    cads_sniff_write_global_header(header);
    cads_storage_write(file, header, sizeof(header));

    cads_probe_puts("# sniff: promiscuous capture to /sniff.pcap for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s (snaplen=");
    cads_probe_put_uint(CADS_SNIFF_SNAPLEN);
    cads_probe_puts(")\r\n");

    cads_hal_eth_mac_set_promiscuous(true);

    /* full receive size - see this file's own header on why; shared, not this
     * command's own buffer - see explorer_capture_buffer.h on why. */
    uint8_t* frame = cads_explorer_capture_buffer();
    uint32_t captured = 0u;
    uint32_t write_errors = 0u;
    uint32_t start = cads_hal_ticks_ms();

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint16_t length = cads_hal_eth_mac_receive(frame, CADS_EXPLORER_CAPTURE_BUFFER_SIZE);
        if(length == 0u) continue;

        uint32_t now = cads_hal_ticks_ms();
        uint32_t incl_len = length < CADS_SNIFF_SNAPLEN ? length : CADS_SNIFF_SNAPLEN;

        uint8_t pkt_header[16];
        cads_sniff_write_packet_header(pkt_header, now, incl_len, length);

        int32_t w1 = cads_storage_write(file, pkt_header, sizeof(pkt_header));
        int32_t w2 = (w1 >= 0) ? cads_storage_write(file, frame, incl_len) : -1;

        if(w1 < 0 || w2 < 0) {
            write_errors++;
        } else {
            captured++;
        }
    }

    cads_hal_eth_mac_set_promiscuous(false);
    cads_storage_close(file);
    cads_storage_unmount();

    uint32_t no_descriptor = 0u;
    uint32_t fifo_overflow = 0u;
    cads_hal_eth_mac_missed_frames(&no_descriptor, &fifo_overflow);

    cads_probe_puts("# sniff: done, ");
    cads_probe_put_uint(captured);
    cads_probe_puts(" captured, ");
    cads_probe_put_uint(write_errors);
    cads_probe_puts(" write error(s), ");
    cads_probe_put_uint(no_descriptor);
    cads_probe_puts(" dropped (no RX descriptor free), ");
    cads_probe_put_uint(fifo_overflow);
    cads_probe_puts(" dropped (RX FIFO overflow/runt)\r\n");
}
