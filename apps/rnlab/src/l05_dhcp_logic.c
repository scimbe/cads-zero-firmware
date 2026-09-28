/*
 * CaDS Zero - rnlab L05 (DHCP): pure logic. See l05_dhcp_logic.h.
 */

#include "l05_dhcp_logic.h"

#define DHCP_OFF_OP 0u
#define DHCP_OFF_XID 4u
#define DHCP_OFF_CIADDR 12u
#define DHCP_OFF_YIADDR 16u
#define DHCP_OFF_COOKIE 236u
#define DHCP_OFF_OPTIONS 240u

#define DHCP_OPT_PAD 0u
#define DHCP_OPT_SUBNET_MASK 1u
#define DHCP_OPT_ROUTER 3u
#define DHCP_OPT_DNS 6u
#define DHCP_OPT_REQUESTED_IP 50u
#define DHCP_OPT_LEASE 51u
#define DHCP_OPT_MSG_TYPE 53u
#define DHCP_OPT_SERVER_ID 54u
#define DHCP_OPT_T1 58u
#define DHCP_OPT_T2 59u
#define DHCP_OPT_END 255u

static inline uint16_t get_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t get_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

bool rnlab_l05_parse(const uint8_t* msg, size_t len, rnlab_dhcp_msg_t* out) {
    /* TODO(L05): eine DHCP-Nachricht zerlegen (Aufbau: l05_dhcp_logic.h).
     *  1. Pruefen: msg/out nicht NULL, len >= RNLAB_DHCP_MIN_LEN, op 1 oder 2,
     *     Magic Cookie 99.130.83.99 an Offset 236.
     *  2. *out auf 0 setzen, dann op, xid, ciaddr, yiaddr mit get_be32() lesen.
     *  3. Optionen ab Offset 240 durchlaufen: 0 = Pad (1 Byte), 255 = Ende,
     *     sonst Code, Laenge, Wert. JEDE Laenge vor dem Lesen gegen len
     *     pruefen - die Bytes kommen aus dem Netz.
     *  4. Optionen 53, 51, 58, 59, 1, 3, 6, 50, 54 uebernehmen (has_* setzen),
     *     unbekannte ueberspringen; eine unmoegliche Laenge (z. B. Option 51
     *     mit 3 Byte) -> false. Bei 3/6 zaehlt die erste Adresse.
     *  5. Ohne gueltige Option 53 (Typ 1..8) ist es BOOTP, kein DHCP -> false.
     * Die Tests in tests/unit/test_rnlab_l05.c zeigen die Grenzfaelle. */
    (void)get_be32; /* liest 4 Byte Big Endian - fuer dich */
    (void)msg;
    (void)len;
    (void)out;
    return false;
}

bool rnlab_l05_timers(const rnlab_dhcp_msg_t* msg, uint32_t* t1_s, uint32_t* t2_s) {
    /* TODO(L05): T1/T2 nach RFC 2131 §4.4.5.
     *  - Ohne Lease (has_lease false) -> false.
     *  - Unendliche Lease (RNLAB_DHCP_INFINITE) -> T1 = T2 = unendlich.
     *  - Sonst die Werte des Servers (Option 58/59), falls vorhanden,
     *    andernfalls T1 = 0,5 * Lease und T2 = 0,875 * Lease (abgerundet).
     *    Vorsicht: Lease * 7 passt bei grossen Leases nicht in 32 Bit. */
    (void)msg;
    (void)t1_s;
    (void)t2_s;
    return false;
}

bool rnlab_l05_find_dhcp(const uint8_t* frame, size_t len, const uint8_t** msg, size_t* msg_len) {
    if(!frame || !msg || !msg_len || len < 14u + 20u + 8u) return false;
    if(get_be16(&frame[12]) != 0x0800u) return false;

    const uint8_t* ip = &frame[14];
    size_t ip_avail = len - 14u;
    size_t ihl = (size_t)(ip[0] & 0x0Fu) * 4u;
    if((ip[0] >> 4) != 4u || ihl < 20u || ip_avail < ihl + 8u) return false;
    if(ip[9] != 17u) return false;                       /* UDP */
    if((get_be16(&ip[6]) & 0x3FFFu) != 0u) return false; /* fragment */

    const uint8_t* udp = ip + ihl;
    uint16_t sport = get_be16(&udp[0]);
    uint16_t dport = get_be16(&udp[2]);
    uint16_t ulen = get_be16(&udp[4]);
    bool s_dhcp = sport == RNLAB_DHCP_SERVER_PORT || sport == RNLAB_DHCP_CLIENT_PORT;
    bool d_dhcp = dport == RNLAB_DHCP_SERVER_PORT || dport == RNLAB_DHCP_CLIENT_PORT;
    if(!s_dhcp || !d_dhcp) return false;
    if(ulen < 8u || ulen > ip_avail - ihl) return false;

    *msg = udp + 8u;
    *msg_len = (size_t)ulen - 8u;
    return true;
}

const char* rnlab_l05_type_name(uint8_t msg_type) {
    static const char* const names[] = {
        "?", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NAK", "RELEASE", "INFORM",
    };
    return msg_type < sizeof(names) / sizeof(names[0]) ? names[msg_type] : "?";
}
