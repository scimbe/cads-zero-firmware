/*
 * CaDS Zero - rnlab L03 (IPv4 und Subnetting): board integration.
 *
 * `lab 03 <cmd> [args]` lands in rnlab_l03_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l03_ipv4_subnetting_logic.c.
 *
 * Two halves:
 *  - rnlab_l03_hook_ip4_input() sees every received IPv4 packet before lwIP
 *    looks at it (LWIP_HOOK_IP4_INPUT) and counts it by destination class x
 *    "source in our subnet?". With `lab 03 filter <cidr>` it also eats
 *    packets whose source lies outside that prefix.
 *  - the commands change the mask at runtime (address and gateway stay) and
 *    show what the host now believes is on-link, so the prediction table of
 *    the lesson can be checked one mask at a time.
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "l03_ipv4_subnetting_logic.h"
/* opt.h before def.h: def.h pulls in arch.h first, whose fallback
 * LWIP_DECLARE_MEMORY_ALIGNED lwipopts.h would then redefine. */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "rnlab_args_logic.h"

/* [destination class][0 = source in our subnet, 1 = source elsewhere]. Plain
 * counters: the hook and the command both run on the console task, inside
 * or between cads_net_poll() calls, never concurrently. */
static uint32_t l03_count[RNLAB_L03_DST_COUNT][2];
static uint32_t l03_filtered;
static bool l03_filter_on;
static uint32_t l03_filter_net;
static uint32_t l03_filter_mask;

int rnlab_l03_hook_ip4_input(struct pbuf* p, struct netif* inp) {
    uint32_t src, dst;
    if(!rnlab_l03_header_addrs((const uint8_t*)p->payload, p->len, &src, &dst)) return 0;

    /* The netif's own view, not the configured one: that is what lwIP is
     * about to judge this packet by. */
    uint32_t own = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(inp)));
    uint32_t mask = lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(inp)));

    rnlab_l03_dst_class_t cls = rnlab_l03_classify_dst(dst, own, mask);
    if(cls >= RNLAB_L03_DST_COUNT) cls = RNLAB_L03_DST_OTHER;
    l03_count[cls][rnlab_same_subnet(src, own, mask) ? 0 : 1]++;

    if(l03_filter_on && !rnlab_same_subnet(src, l03_filter_net, l03_filter_mask)) {
        l03_filtered++;
        pbuf_free(p); /* consumed: the hook contract makes freeing our job */
        return 1;
    }
    return 0;
}

static void l03_write_ip(cads_cli_session_t* session, uint32_t ip) {
    char text[16];
    cads_fmt_ipv4(text, sizeof(text), ip);
    cads_cli_write(session, text);
}

static void l03_write_prefix(cads_cli_session_t* session, uint32_t mask) {
    int prefix = rnlab_l03_mask_to_prefix(mask);
    cads_cli_write(session, " (/");
    if(prefix < 0) {
        cads_cli_write(session, "?");
    } else {
        cads_cli_write_uint(session, (uint32_t)prefix);
    }
    cads_cli_write(session, ")");
}

/* Address plan of the current configuration plus the warnings a wrong mask
 * deserves - printed by `show` and after every `netmask`. */
static void l03_show(cads_cli_session_t* session) {
    cads_net_status_t st;
    cads_net_status(&st);

    cads_cli_write(session, "ip:        ");
    l03_write_ip(session, st.ip_addr);
    cads_cli_write(session, "\r\nmaske:     ");
    l03_write_ip(session, st.netmask);
    l03_write_prefix(session, st.netmask);
    cads_cli_write(session, "\r\nnetz:      ");
    l03_write_ip(session, rnlab_l03_network(st.ip_addr, st.netmask));
    cads_cli_write(session, "\r\nbroadcast: ");
    l03_write_ip(session, rnlab_l03_broadcast(st.ip_addr, st.netmask));
    cads_cli_write(session, "\r\ngateway:   ");
    l03_write_ip(session, st.gw_addr);
    cads_cli_write(session, rnlab_same_subnet(st.gw_addr, st.ip_addr, st.netmask) ? " (im Netz)\r\n"
                                                                                  : " (NICHT im Netz)\r\n");

    int prefix = rnlab_l03_mask_to_prefix(st.netmask);
    if(prefix >= 0 && prefix <= 30 && st.ip_addr != 0u) {
        if(st.ip_addr == rnlab_l03_broadcast(st.ip_addr, st.netmask)) {
            cads_cli_write(session, "Warnung: eigene Adresse = Broadcast-Adresse\r\n");
        } else if(st.ip_addr == rnlab_l03_network(st.ip_addr, st.netmask)) {
            cads_cli_write(session, "Warnung: eigene Adresse = Netzadresse\r\n");
        }
    }
}

static void l03_cmd_netmask(cads_cli_session_t* session, const char* text) {
    uint32_t mask;
    if(!rnlab_l03_parse_mask(text, &mask)) {
        cads_cli_write(session, "? ungueltige Maske (z.B. 255.255.255.128, /25 oder 25)\r\n");
        return;
    }
    cads_net_config_t config;
    cads_net_get_config(&config);
    if(config.use_dhcp) {
        cads_cli_write(session, "? DHCP aktiv - erst 'lab net static'\r\n");
        return;
    }
    /* Only the mask changes: same address, so lwIP keeps every open TCP
     * connection (this Telnet session included) - what changes is which
     * destinations it now believes are on-link. */
    config.netmask = mask;
    cads_net_set_config(&config);
    l03_show(session);
}

static void l03_write_route(cads_cli_session_t* session, uint32_t dst, uint32_t* next_hop) {
    cads_net_status_t st;
    cads_net_status(&st);

    static const char* const class_names[RNLAB_L03_DST_COUNT] = {"eigene Adresse", "Broadcast", "Multicast",
                                                                 "Unicast"};
    rnlab_l03_dst_class_t cls = rnlab_l03_classify_dst(dst, st.ip_addr, st.netmask);
    if(cls >= RNLAB_L03_DST_COUNT) cls = RNLAB_L03_DST_OTHER;

    *next_hop = rnlab_l03_next_hop(st.ip_addr, st.netmask, st.gw_addr, dst);
    cads_cli_write(session, "ziel:      ");
    l03_write_ip(session, dst);
    cads_cli_write(session, " (");
    cads_cli_write(session, class_names[cls]);
    cads_cli_write(session, ")\r\nnext hop:  ");
    if(*next_hop == 0u) {
        cads_cli_write(session, "keiner (kein Gateway)\r\n");
    } else {
        l03_write_ip(session, *next_hop);
        /* "direkt" only when dst itself is on-link. With /30 the gateway .1
         * is off-link too, yet next_hop == dst - comparing the two would
         * call that "direkt" and hide exactly what the lesson asks about. */
        bool on_link = cls == RNLAB_L03_DST_BROADCAST || rnlab_same_subnet(dst, st.ip_addr, st.netmask);
        if(on_link) {
            cads_cli_write(session, " (direkt)\r\n");
        } else if(rnlab_same_subnet(st.gw_addr, st.ip_addr, st.netmask)) {
            cads_cli_write(session, " (Gateway)\r\n");
        } else {
            /* lwIP ARPs for it anyway (etharp_output does not check). */
            cads_cli_write(session, " (Gateway, selbst nicht im Netz)\r\n");
        }
    }
}

/* Route decision, then the two things that can fail on the way: ARP for the
 * next hop, and the echo reply itself. Blocks for up to 2 x 400 ms, polling
 * lwIP itself like cads_net_ping() does - 400 ms is ample on one cable and
 * keeps each silent stretch below a client's idle timeout (rnlab.py 0.5 s). */
#define L03_PROBE_TIMEOUT_MS 400u
static void l03_cmd_probe(cads_cli_session_t* session, uint32_t dst) {
    uint32_t next_hop;
    l03_write_route(session, dst, &next_hop);
    if(next_hop == 0u) return;

    cads_net_status_t st;
    cads_net_status(&st);
    bool broadcast = rnlab_l03_classify_dst(dst, st.ip_addr, st.netmask) == RNLAB_L03_DST_BROADCAST;
    if(!broadcast) {
        uint8_t mac[6];
        cads_cli_write(session, "arp:       ");
        if(cads_net_arp_probe(next_hop, L03_PROBE_TIMEOUT_MS, mac)) {
            char text[18];
            cads_fmt_mac(text, sizeof(text), mac);
            cads_cli_write(session, text);
            cads_cli_write(session, "\r\n");
        } else {
            cads_cli_write(session, "keine Antwort\r\n");
        }
    }

    uint32_t rtt_ms = 0u;
    cads_cli_write(session, "ping:      ");
    if(cads_net_ping(dst, L03_PROBE_TIMEOUT_MS, &rtt_ms)) {
        cads_cli_write(session, "Antwort nach ");
        cads_cli_write_uint(session, rtt_ms);
        cads_cli_write(session, " ms\r\n");
    } else {
        cads_cli_write(session, "keine Antwort\r\n");
    }
}

static void l03_cmd_stats(cads_cli_session_t* session) {
    /* 12-char labels, two 10-wide columns. */
    static const char* const rows[RNLAB_L03_DST_COUNT] = {"eigene IP   ", "Broadcast   ", "Multicast   ",
                                                          "fremdes Ziel"};
    cads_cli_write(session, "Ziel\\Quelle    eig. Netz  fremd.Netz\r\n");
    for(int i = 0; i < RNLAB_L03_DST_COUNT; i++) {
        char num[12];
        cads_cli_write(session, rows[i]);
        cads_cli_write(session, "  ");
        cads_fmt_uint_pad(num, sizeof(num), l03_count[i][0], 10u, ' ');
        cads_cli_write(session, num);
        cads_cli_write(session, "  ");
        cads_fmt_uint_pad(num, sizeof(num), l03_count[i][1], 10u, ' ');
        cads_cli_write(session, num);
        cads_cli_write(session, "\r\n");
    }
    cads_cli_write(session, "filter:    ");
    if(l03_filter_on) {
        l03_write_ip(session, l03_filter_net);
        cads_cli_write(session, "/");
        cads_cli_write_uint(session, (uint32_t)rnlab_l03_mask_to_prefix(l03_filter_mask));
    } else {
        cads_cli_write(session, "aus");
    }
    cads_cli_write(session, ", verworfen: ");
    cads_cli_write_uint(session, l03_filtered);
    cads_cli_write(session, "\r\n");
}

static void l03_cmd_filter(cads_cli_session_t* session, const char* text) {
    if(cads_str_equal(text, "off") || cads_str_equal(text, "aus")) {
        l03_filter_on = false;
        cads_cli_write(session, "filter aus\r\n");
        return;
    }
    uint32_t net, mask;
    if(!rnlab_l03_parse_cidr(text, &net, &mask)) {
        cads_cli_write(session, "? Aufruf: lab 03 filter <netz/praefix> | off\r\n");
        return;
    }
    l03_filter_net = net;
    l03_filter_mask = mask;
    l03_filter_on = true;
    cads_cli_write(session, "filter an: nur Quellen aus ");
    l03_write_ip(session, net);
    cads_cli_write(session, "/");
    cads_cli_write_uint(session, (uint32_t)rnlab_l03_mask_to_prefix(mask));
    cads_cli_write(session, "\r\n");
}

static void l03_help(cads_cli_session_t* session) {
    /* One write per line: cads_cli_write() stops after 4 * CADS_CLI_LINE_MAX
     * (384) bytes, and this text is longer. */
    static const char* const lines[] = {
        "lab 03 show              Adresse, Maske, Netz, Broadcast, Gateway\r\n",
        "lab 03 netmask <m>       Maske setzen (255.255.255.128 | /25 | 25)\r\n",
        "lab 03 route <ip>        Zielklasse und next hop\r\n",
        "lab 03 probe <ip>        route + ARP fuer next hop + ping\r\n",
        "lab 03 stats             IPv4-Empfang nach Ziel/Quelle\r\n",
        "lab 03 reset             Zaehler auf 0\r\n",
        "lab 03 filter <cidr>|off nur Quellen aus <cidr> annehmen\r\n",
    };
    for(size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) cads_cli_write(session, lines[i]);
}

void rnlab_l03_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0 || cads_str_equal(argv[0], "show")) {
        l03_show(session);
        return;
    }
    if(cads_str_equal(argv[0], "help")) {
        l03_help(session);
        return;
    }
    if(cads_str_equal(argv[0], "stats")) {
        l03_cmd_stats(session);
        return;
    }
    if(cads_str_equal(argv[0], "reset")) {
        for(int i = 0; i < RNLAB_L03_DST_COUNT; i++) l03_count[i][0] = l03_count[i][1] = 0u;
        l03_filtered = 0u;
        cads_cli_write(session, "Zaehler geloescht\r\n");
        return;
    }
    if(argc == 2 && cads_str_equal(argv[0], "netmask")) {
        l03_cmd_netmask(session, argv[1]);
        return;
    }
    if(argc == 2 && cads_str_equal(argv[0], "filter")) {
        l03_cmd_filter(session, argv[1]);
        return;
    }
    uint32_t ip;
    if(argc == 2 && (cads_str_equal(argv[0], "route") || cads_str_equal(argv[0], "probe")) &&
       rnlab_parse_ipv4(argv[1], &ip)) {
        if(argv[0][0] == 'r') {
            uint32_t next_hop;
            l03_write_route(session, ip, &next_hop);
        } else {
            l03_cmd_probe(session, ip);
        }
        return;
    }
    cads_cli_write(session, "? unbekannt ('lab 03 help')\r\n");
}
