/*
 * CaDS Zero - active tooling frame builders/parsers (M9).
 *
 * The portable half of modules/netx: pure functions over caller-owned
 * stack-local buffers, no HAL, no lwIP, no static state. This is the part
 * of the "Active Net Tools" suite that can be - and is - unit-tested on
 * the host with golden-byte comparison (tests/unit/test_netx_frame.c),
 * the same split cads/toolbox already established for the passive
 * detectors (arpwatch/dhcpwatch/l2discover).
 *
 * WHY CALLER-OWNED BUFFERS
 * ------------------------
 * This firmware has no heap and a 256 B RAM margin against the linker's
 * 48 K floor. A per-builder static staging buffer would be ~1.5 KB each,
 * which is more than the entire suite can afford. Every builder here
 * writes into a buffer the caller supplies on the stack (frame views
 * run in the UI task, whose stack is sized for this), and returns the
 * number of bytes written (0 on a capacity overrun, so a too-small
 * caller buffer is a silent no-op rather than an overrun).
 *
 * BYTE ORDER CONVENTION
 * ---------------------
 * IPs and ports are passed in HOST byte order; builders put them on the
 * wire in network (big-endian) order. Lifetimes likewise. MACs and raw
 * prefixes are byte arrays, passed through untouched.
 *
 * WHAT THIS DOES NOT DO
 * ---------------------
 * No transmission, no capture, no policy. The TX/RX surface lives in
 * rawio.h (board/sim split); the decision to send lives in apps/active.
 * Builders here are mechanics only - they produce well-formed frames;
 * whether to put them on the wire is a question for the caller.
 */

#ifndef CADS_NETX_FRAME_H
#define CADS_NETX_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cap every builder checks against. 1536 B > 1522 B max VLAN frame, so a
 *  single-tag or double-tag (QinQ) frame always fits. Callers typically
 *  declare one buffer of this size on the stack and reuse it. */
#define CADS_NETX_FRAME_MAX 1536u

/** EtherType constants the builders use (kept here so callers do not
 *  re-derive them and so tests can assert exact on-wire values). */
#define CADS_NETX_ETHERTYPE_IPV4  0x0800u
#define CADS_NETX_ETHERTYPE_ARP   0x0806u
#define CADS_NETX_ETHERTYPE_VLAN  0x8100u
#define CADS_NETX_ETHERTYPE_IPV6  0x86DDu
#define CADS_NETX_ETHERTYPE_EAPOL 0x888Eu

/* --- Ethernet ------------------------------------------------------------- */

/**
 * Build a plain Ethernet II frame: 14-byte header + `payload`.
 * Returns the total frame length, or 0 if `cap` cannot hold it.
 */
uint16_t cads_netx_build_eth(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6], uint16_t ethertype,
    const uint8_t* payload, uint16_t payload_len);

/**
 * Build a single 802.1Q-tagged Ethernet frame (outer TPID 0x8100, the
 * "access port's native VID" case for VLAN hopping). `vlan_vid` uses only
 * its low 12 bits (PCP/DEI are zero - this firmware sends priority-best-
 * effort forged frames, not QoS-marked ones).
 */
uint16_t cads_netx_build_vlan(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint16_t vlan_vid, uint16_t ethertype,
    const uint8_t* payload, uint16_t payload_len);

/**
 * Build a double-tagged QinQ frame: outer tag 0x8100 (the access port's
 * native VID, which the switch strips on ingress) then inner tag 0x8100
 * (the target VLAN the attacker actually wants to reach), then the
 * payload. This is the "VLAN hopping" frame shape - the switch forwards
 * the inner-tagged frame into the target VLAN because it never saw the
 * inner tag as an outer tag.
 */
uint16_t cads_netx_build_vlan_qinq(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint16_t outer_vid, uint16_t inner_vid, uint16_t ethertype,
    const uint8_t* payload, uint16_t payload_len);

/* --- ARP ------------------------------------------------------------------ */

/**
 * Build a 42-byte Ethernet/ARP REPLY (opcode 2). `sender_mac`/`sender_ip`
 * are the claim an ARP cache will learn ("this IP is at this MAC"); the
 * frame is unicast to `dst` (the victim) so only that host's cache is
 * poisoned. The target fields are set to the sender's (a normal ARP
 * reply shape); `target_ip_host` is echoed into the ARP target IP.
 */
uint16_t cads_netx_build_arp_reply(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    const uint8_t sender_mac[6], uint32_t sender_ip_host,
    uint32_t target_ip_host);

/**
 * Build a 42-byte gratuitous ARP (a REPLY where sender IP == target IP
 * and the destination is broadcast). Every host that receives it
 * refreshes its cache to map `claimed_ip` to `src_mac` whether or not
 * it had an entry before - the cache-poisoner's broadcast sweep shape.
 */
uint16_t cads_netx_build_arp_gratuitous(uint8_t* out, uint16_t cap,
    const uint8_t src[6], uint32_t claimed_ip_host);

/**
 * Build a 42-byte Ethernet/ARP REQUEST (opcode 1). `dst` is typically the
 * broadcast (a "who-has" probe); `sender_mac`/`sender_ip_host` are the
 * requester, `target_ip_host` is the IP being resolved. The target MAC
 * field is zero (the unknown being asked for). Used standalone as a probe
 * and as the inner payload of a VLAN-hopping frame (callers take bytes
 * 14..41 - the 28-byte ARP message - as the VLAN payload under ethertype
 * 0x0806).
 */
uint16_t cads_netx_build_arp_request(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    const uint8_t sender_mac[6], uint32_t sender_ip_host,
    uint32_t target_ip_host);

/* --- IPv4 / UDP / TCP ----------------------------------------------------- */

/**
 * Build an Ethernet/IPv4 frame carrying `payload` under protocol
 * `proto` (e.g. 17 UDP, 6 TCP, 1 ICMP). The IPv4 header checksum is
 * computed and filled; payload bytes are copied verbatim (no transport
 * checksum here - the UDP/TCP builders below wrap this with their own
 * pseudo-header checksums).
 */
uint16_t cads_netx_build_ipv4(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint8_t proto, uint32_t src_ip_host, uint32_t dst_ip_host,
    const uint8_t* payload, uint16_t payload_len);

/**
 * Build an Ethernet/IPv4/UDP frame with both the IP header checksum and
 * the UDP checksum (over the IPv4 pseudo-header + UDP header + payload)
 * computed and filled. `src_port`/`dst_port` are host byte order.
 */
uint16_t cads_netx_build_udp(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t src_ip_host, uint32_t dst_ip_host,
    uint16_t src_port, uint16_t dst_port,
    const uint8_t* payload, uint16_t payload_len);

/**
 * Build an Ethernet/IPv4/TCP segment with only the RST flag set, the
 * sequence number set to `seq_host` (the peer's rcv_nxt for an injected
 * tear-down), and an ACK number of 0. The IP and TCP checksums are
 * computed and filled. No payload - a RST is a header-only segment.
 */
uint16_t cads_netx_build_tcp_rst(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t src_ip_host, uint32_t dst_ip_host,
    uint16_t src_port, uint16_t dst_port,
    uint32_t seq_host);

/* --- ICMPv6 (IPv6 is off in lwIP; RAs are hand-built on raw L2) ----------- */

/**
 * Build an Ethernet/IPv6/ICMPv6 Router Advertisement (type 134) carrying
 * a Prefix Information option (type 3) for `prefix` (a /64) and an MTU
 * option (type 5). Source link-local address is fe80::EUI-64 derived
 * from `src_mac`; destination is the all-nodes multicast ff02::1; the
 * Ethernet destination is 33:33:00:00:00:01. Hop limit is 255 (the value
 * a receiver requires of an RA, RFC 4861). The ICMPv6 checksum is
 * computed over the IPv6 pseudo-header + ICMPv6 message.
 *
 * `valid_lt_host`/`preferred_lt_host` are the prefix lifetimes in
 * seconds; `mtu` is advertised in the MTU option (0 suppresses the MTU
 * option entirely). All host byte order.
 *
 * No lwIP IPv6 is needed for this - the entire IPv6 header is built
 * here, byte for byte, on raw L2.
 */
uint16_t cads_netx_build_icmpv6_ra(uint8_t* out, uint16_t cap,
    const uint8_t src_mac[6],
    const uint8_t prefix[16],
    uint32_t valid_lt_host, uint32_t preferred_lt_host, uint16_t mtu);

/* --- CoAP (hand-built on raw UDP, no lib) --------------------------------- */

/**
 * Build a minimal CoAP GET request (RFC 7252) for `uri_path` - just the CoAP
 * layer (4-byte header + URI-Path options), NOT wrapped in UDP/IP. The
 * caller passes the result as the UDP payload to cads_netx_build_udp. Used
 * by the MQTT/CoAP reverse beacon to GET /.well-known/core (the standard
 * resource-discovery path) and so announce the board's presence.
 *
 * The message is Confirmable (Type=CON), token-less, with a caller-supplied
 * Message ID. `uri_path` is '/'-separated; each segment becomes one
 * URI-Path option (option number 11). Only the simple delta/length nibble
 * form is used (segment length <= 12), so paths with longer segments are a
 * build error (returns 0) - /.well-known/core's longest segment is 11, so
 * it fits. Returns the CoAP message length, or 0 on overflow/bad segment.
 */
uint16_t cads_netx_build_coap_get(uint8_t* out, uint16_t cap,
    uint16_t msg_id, const char* uri_path);

/* --- DHCP (rogue server: OFFER/ACK, hand-built on raw L2) ------------------ */

/** Parsed DHCP message fields a rogue OFFER/ACK must echo. The parser takes the
 *  DHCP message - the UDP payload, post-UDP-header - which is the shape the
 *  rogue DHCP engine sees from its raw UDP PCB recv callback (lwIP strips the
 *  UDP header before udp_recv). */
typedef struct {
    uint8_t  client_mac[6];    /* chaddr: who to address the reply to          */
    uint32_t xid_host;         /* transaction id to echo                       */
    uint32_t requested_ip_host;/* option 50 (DISCOVER), 0 if absent           */
    uint32_t client_ip_host;   /* ciaddr (BOUND/RENEWING), 0 if zero           */
} cads_netx_dhcp_discover_t;

/** Parse a DHCP message (UDP payload). Returns false if too short to be a
 *  DISCOVER, or if op is not BOOTREQUEST / htype not Ethernet. Option 50
 *  (Requested IP Address) is extracted when present; absence is not an
 *  error (requested_ip_host stays 0). */
bool cads_netx_parse_dhcp_discover(const uint8_t* msg, uint16_t len,
    cads_netx_dhcp_discover_t* out);

/** Build an Ethernet/IPv4/UDP/DHCP OFFER (message type 2). `server_ip_host`
 *  is the rogue server's IP (also siaddr and the server-id option); the lease
 *  offered is `client_ip_host`, addressed to `client_mac` with `xid_host`
 *  echoed. `router_ip_host`/`dns_ip_host` are the router (option 3) and DNS
 *  (option 6) options - both set to the rogue server's IP by the engine so
 *  the victim's traffic is sinkholed through us. `lease_host` is the lease
 *  time in seconds. A /24 subnet mask (option 1) is always offered. The
 *  frame is broadcast (engine passes the broadcast dst MAC and 255.255.255.255
 *  dst IP) because the client does not yet have an address. */
uint16_t cads_netx_build_dhcp_offer(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t server_ip_host, uint32_t client_ip_host,
    const uint8_t client_mac[6], uint32_t xid_host,
    uint32_t lease_host, uint32_t router_ip_host, uint32_t dns_ip_host);

/** Build an Ethernet/IPv4/UDP/DHCP ACK (message type 5) - same shape as the
 *  OFFER with message type 5. Sent in response to a REQUEST confirming a
 *  lease the OFFER proposed. */
uint16_t cads_netx_build_dhcp_ack(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t server_ip_host, uint32_t client_ip_host,
    const uint8_t client_mac[6], uint32_t xid_host,
    uint32_t lease_host, uint32_t router_ip_host, uint32_t dns_ip_host);

/* --- DNS (rogue sinkhole: A -> sinkhole IP / NXDOMAIN) --------------------- */

/** Parsed DNS query fields a forged response must echo. `question_len` is the
 *  byte length of the question section (the name labels + qtype + qclass),
 *  measured from offset 12 of the DNS message; the response echoes that span
 *  verbatim. */
typedef struct {
    uint16_t txid_host;       /* transaction id to echo                       */
    uint16_t question_len;    /* bytes of the question section (offset 12..)  */
    uint16_t qtype_host;      /* A=1, AAAA=28, ... (only A is answered)        */
    uint16_t qclass_host;     /* IN=1                                         */
} cads_netx_dns_query_t;

/** Parse a DNS query (UDP payload). Returns false if too short, not a
 *  standard query (QR=0, opcode 0), or with a question count other than 1.
 *  Name compression pointers in a query are rejected (a fresh client query
 *  does not compress). `question_offset_out`, if non-NULL, receives the offset
 *  of the question section within the message (always 12 for a valid query). */
bool cads_netx_parse_dns_query(const uint8_t* msg, uint16_t len,
    cads_netx_dns_query_t* out, uint16_t* question_offset_out);

/** Build an Ethernet/IPv4/UDP/DNS response echoing `query_msg`'s question
 *  section and answering with `answer_ip_host` (an A record), or NXDOMAIN
 *  (RCODE=3, no answer record). `query_msg`/`query_len` is the original DNS
 *  query message; `client_port` is the query's source port, which becomes the
 *  response's destination port. `server_ip_host`/`client_ip_host` set the IP
 *  header (the response is unicast to the client, which has an address by the
 *  time it queries DNS). The answer uses a name pointer (0xC00C) back to the
 *  echoed question to keep the response compact. */
uint16_t cads_netx_build_dns_response(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t server_ip_host, uint32_t client_ip_host,
    uint16_t client_port,
    const uint8_t* query_msg, uint16_t query_len,
    uint32_t answer_ip_host, bool nxdomain);

/* --- EAPOL (802.1X supplicant bypass) -------------------------------------- */

/** Parsed EAPOL frame fields for MAC cloning. `eap_type` is 0 unless the EAP
 *  packet is a Request/Response carrying a type byte (Identity=1, etc.). */
typedef struct {
    uint8_t  supplicant_mac[6]; /* Ethernet src of the EAPOL frame            */
    uint8_t  eapol_type;        /* 0=EAP-Packet, 1=Start, 2=Logoff, 3=Key      */
    uint8_t  eap_code;          /* 1=Request,2=Response,3=Success,4=Failure   */
    uint8_t  eap_id;            /* EAP identifier                             */
    uint8_t  eap_type;          /* 1=Identity, ... (0 if not Request/Response) */
} cads_netx_eapol_t;

/** Parse a full captured Ethernet frame as EAPOL (ethertype 0x888E). The 802.1X
 *  engine sees EAPOL via promiscuous capture (lwIP never delivers it), so the
 *  parser takes the whole frame. Returns false if not EAPOL or too short. */
bool cads_netx_parse_eapol(const uint8_t* frame, uint16_t len,
    cads_netx_eapol_t* out);

/* --- TCP (RST daemon flow tracking) --------------------------------------- */

/** Parsed IPv4/TCP segment fields a RST daemon needs. `seq`/`ack` are the
 *  segment's own numbers; `payload_len` is the TCP payload size. For RST
 *  injection the daemon uses the peer's rcv_nxt, which for a segment carrying
 *  payload is seq+payload_len, and for a pure ACK is the ack field. `has_vlan`
 *  /`vlan_vid` carry the outer 802.1Q tag when present (VLAN-hopping case). */
typedef struct {
    bool     has_vlan;
    uint16_t vlan_vid;
    uint32_t src_ip_host;
    uint32_t dst_ip_host;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_host;
    uint32_t ack_host;
    uint8_t  flags;       /* TCP flags byte (RST=0x04, ACK=0x10, etc.)        */
    uint16_t payload_len;
} cads_netx_tcp_seg_t;

/** Parse a full captured Ethernet frame as IPv4/TCP, skipping one or two VLAN
 *  tags. The TCP RST engine sees other hosts' flows via promiscuous capture,
 *  so the parser takes the whole frame. Returns false if not IPv4/TCP or too
 *  short. Non-IPv4 ethertypes (ARP, IPv6) are rejected. */
bool cads_netx_parse_tcp_seg(const uint8_t* frame, uint16_t len,
    cads_netx_tcp_seg_t* out);

/* --- MQTT CONNECT (reverse beacon, Phase 2) ------------------------------ */

/** Build a minimal MQTT v3.1.1 CONNECT (no will, no user/pass, clean session
 *  only) - just the MQTT message, NOT wrapped in TCP/IP. The beacon engine
 *  sends it over a raw TCP PCB to :1883. `client_id` is the UTF-8 client
 *  identifier (length <= 100); `keepalive_host` is the keep-alive in seconds
 *  (0 = none). Returns the MQTT message length, or 0 on overflow / a client
 *  id longer than 117 bytes (remaining-length must fit one byte). */
uint16_t cads_netx_build_mqtt_connect(uint8_t* out, uint16_t cap,
    const char* client_id, uint16_t keepalive_host);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NETX_FRAME_H */