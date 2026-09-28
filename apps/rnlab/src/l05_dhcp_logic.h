/*
 * CaDS Zero - rnlab L05 (DHCP): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l05.c links this file
 * directly on the host. Board integration lives in l05_dhcp.c.
 *
 * A DHCP message (RFC 2131) is a BOOTP message (RFC 951) with a magic
 * cookie and a list of options behind it:
 *
 *   offset  size  field
 *        0     1  op      1 = BOOTREQUEST (client), 2 = BOOTREPLY (server)
 *        1     1  htype   1 = Ethernet
 *        2     1  hlen    6
 *        3     1  hops
 *        4     4  xid     transaction id, chosen by the client
 *        8     2  secs
 *       10     2  flags   bit 15 = broadcast
 *       12     4  ciaddr  client address (only if it already has one)
 *       16     4  yiaddr  "your" address - what the server hands out
 *       20     4  siaddr  next server (boot server, not the DHCP server!)
 *       24     4  giaddr  relay agent
 *       28    16  chaddr  client hardware address
 *       44    64  sname
 *      108   128  file
 *      236     4  magic cookie 99.130.83.99
 *      240     -  options: code, length, value ... 255 (end); 0 = pad
 *
 * All addresses in this file are host byte order, 192.168.2.1 = 0xC0A80201,
 * the same convention as the `lab` framework (rnlab.c) and cads_fmt_ipv4().
 */

#ifndef RNLAB_L05_DHCP_LOGIC_H
#define RNLAB_L05_DHCP_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNLAB_DHCP_SERVER_PORT 67u
#define RNLAB_DHCP_CLIENT_PORT 68u

/** Fixed BOOTP part plus magic cookie - the smallest valid DHCP message. */
#define RNLAB_DHCP_MIN_LEN 240u

/** "Infinite" lease (RFC 2131 §3.3): all timers stay off. */
#define RNLAB_DHCP_INFINITE 0xFFFFFFFFu

/** Option 53, DHCP message type. */
enum {
    RNLAB_DHCP_DISCOVER = 1,
    RNLAB_DHCP_OFFER = 2,
    RNLAB_DHCP_REQUEST = 3,
    RNLAB_DHCP_DECLINE = 4,
    RNLAB_DHCP_ACK = 5,
    RNLAB_DHCP_NAK = 6,
    RNLAB_DHCP_RELEASE = 7,
    RNLAB_DHCP_INFORM = 8,
};

/** The fields of one DHCP message that this lesson looks at. */
typedef struct {
    uint8_t op;       /**< 1 = request (client), 2 = reply (server) */
    uint32_t xid;     /**< transaction id */
    uint32_t ciaddr;  /**< client's current address, 0 if none */
    uint32_t yiaddr;  /**< address offered/assigned by the server */
    uint8_t msg_type; /**< option 53, RNLAB_DHCP_DISCOVER ... _INFORM */

    bool has_lease;   /**< option 51 present */
    bool has_t1;      /**< option 58 present */
    bool has_t2;      /**< option 59 present */
    uint32_t lease_s; /**< option 51, lease time in seconds */
    uint32_t t1_s;    /**< option 58, renewal time T1 in seconds */
    uint32_t t2_s;    /**< option 59, rebinding time T2 in seconds */

    uint32_t subnet_mask;  /**< option 1, 0 = absent */
    uint32_t router;       /**< option 3, first router, 0 = absent */
    uint32_t dns;          /**< option 6, first DNS server, 0 = absent */
    uint32_t requested_ip; /**< option 50 (in a REQUEST), 0 = absent */
    uint32_t server_id;    /**< option 54, 0 = absent */
} rnlab_dhcp_msg_t;

/**
 * Parse one DHCP message (the UDP payload, starting at `op`).
 *
 * Returns true and fills `out` only for a well-formed DHCP message: at
 * least RNLAB_DHCP_MIN_LEN bytes, op 1 or 2, the magic cookie, an option
 * list that stays inside `len`, the fixed-size options (1, 3, 6, 50, 51,
 * 53, 54, 58, 59) with a plausible length, and an option 53 (without it,
 * it is plain BOOTP, not DHCP). Unknown options are skipped. Everything
 * else returns false; `out` is then unspecified.
 */
bool rnlab_l05_parse(const uint8_t* msg, size_t len, rnlab_dhcp_msg_t* out);

/**
 * T1 and T2 for a DHCPACK, in seconds (RFC 2131 §4.4.5): the server's
 * options 58/59 if present, otherwise T1 = 0.5 * lease and
 * T2 = 0.875 * lease (rounded down). An infinite lease gives infinite T1/T2.
 * Returns false if `msg` carries no lease time (option 51).
 */
bool rnlab_l05_timers(const rnlab_dhcp_msg_t* msg, uint32_t* t1_s, uint32_t* t2_s);

/**
 * Find the DHCP message in a raw Ethernet frame: IPv4, UDP, source or
 * destination port 67/68. Returns true with `*msg`/`*msg_len` pointing
 * into `frame` (length taken from the UDP header, so Ethernet padding is
 * not counted). Provided - not part of the exercise.
 */
bool rnlab_l05_find_dhcp(const uint8_t* frame, size_t len, const uint8_t** msg, size_t* msg_len);

/** "DISCOVER" ... "INFORM", "?" for anything else. Provided. */
const char* rnlab_l05_type_name(uint8_t msg_type);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L05_DHCP_LOGIC_H */
