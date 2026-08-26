/*
 * CaDS Zero - iperf throughput test (board side).
 *
 * Thin glue over lwIP's own lwiperf app (lib/lwip/src/apps/lwiperf) - the
 * measurement logic is third-party, vendored code, not reimplemented here,
 * the same "wire in the well-tested thing" choice this project already
 * made for littlefs and lwIP itself. This file only starts the server,
 * pumps cads_net_poll() so its TCP state machine actually runs, and prints
 * whatever report lwiperf hands back when a session ends.
 *
 * Expect well under 100 Mbit/s if a real client ever drives this: the
 * STM32F429's ETH has DMA and checksum offload, but this driver does not
 * use the checksum offload (hal_eth_mac.h's own file header explains why),
 * so every byte is still checksummed in software, and TCP_MSS=536/
 * TCP_WND=2144 (modules/net/include/lwipopts.h) cap how much can be in
 * flight at once regardless of what the silicon could otherwise do.
 */

#include "explorer_iperf_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h"

#include "lwip/apps/lwiperf.h"

static const char* cads_iperf_report_name(enum lwiperf_report_type type) {
    switch(type) {
    case LWIPERF_TCP_DONE_SERVER: return "done";
    case LWIPERF_TCP_ABORTED_LOCAL: return "aborted (local)";
    case LWIPERF_TCP_ABORTED_LOCAL_DATAERROR: return "aborted (data error)";
    case LWIPERF_TCP_ABORTED_LOCAL_TXERROR: return "aborted (tx error)";
    case LWIPERF_TCP_ABORTED_REMOTE: return "aborted (remote)";
    default: return "?"; /* LWIPERF_TCP_DONE_CLIENT never fires - server mode only, see this file's header */
    }
}

static void cads_iperf_report(
    void* arg,
    enum lwiperf_report_type report_type,
    const ip_addr_t* local_addr,
    u16_t local_port,
    const ip_addr_t* remote_addr,
    u16_t remote_port,
    u32_t bytes_transferred,
    u32_t ms_duration,
    u32_t bandwidth_kbitpsec) {
    (void)arg;
    (void)local_addr;
    (void)local_port;

    char remote_text[16];
    cads_fmt_ipv4(remote_text, sizeof(remote_text), lwip_ntohl(ip4_addr_get_u32(remote_addr)));

    cads_probe_puts("# iperf: session ");
    cads_probe_puts(cads_iperf_report_name(report_type));
    cads_probe_puts(" from ");
    cads_probe_puts(remote_text);
    cads_probe_puts(":");
    cads_probe_put_uint(remote_port);
    cads_probe_puts(" bytes=");
    cads_probe_put_uint(bytes_transferred);
    cads_probe_puts(" ms=");
    cads_probe_put_uint(ms_duration);
    cads_probe_puts(" kbps=");
    cads_probe_put_uint(bandwidth_kbitpsec);
    cads_probe_puts("\r\n");
}

void cads_explorer_iperf_demo(uint32_t seconds) {
    if(!seconds) seconds = 30u;

    cads_net_init(cads_explorer_net_mac());

    void* session = lwiperf_start_tcp_server_default(cads_iperf_report, NULL);
    cads_probe_puts(session ? "# iperf: server listening on TCP :5001 (iperf2 default)\r\n" :
                               "# iperf: failed to start server\r\n");

    cads_probe_puts("# iperf: running for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_net_poll();
        cads_hal_delay_ms(10u);
    }

    if(session) lwiperf_abort(session);
    cads_probe_puts("# iperf: done\r\n");
}

void cads_explorer_iperf_client_demo(uint32_t target, uint32_t seconds) {
    if(!seconds) seconds = 30u;

    cads_net_init(cads_explorer_net_mac());

    ip_addr_t target_addr;
    ip4_addr_set_u32(ip_2_ip4(&target_addr), lwip_htonl(target));
#if LWIP_IPV6
    IP_SET_TYPE_VAL(target_addr, IPADDR_TYPE_V4);
#endif

    char target_text[16];
    cads_fmt_ipv4(target_text, sizeof(target_text), target);
    cads_probe_puts("# iperf: connecting to ");
    cads_probe_puts(target_text);
    cads_probe_puts(":5001\r\n");

    void* session = lwiperf_start_tcp_client_default(&target_addr, cads_iperf_report, NULL);
    if(!session) {
        cads_probe_puts("# iperf: failed to start\r\n");
        return;
    }

    uint32_t start = cads_hal_ticks_ms();
    while((cads_hal_ticks_ms() - start) < seconds * 1000u) {
        cads_net_poll();
        cads_hal_delay_ms(10u);
    }

    lwiperf_abort(session);
    cads_probe_puts("# iperf: done\r\n");
}
