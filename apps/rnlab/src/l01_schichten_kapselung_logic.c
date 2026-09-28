#include "l01_schichten_kapselung_logic.h"

/* Helpers for your decoder. Network byte order is big-endian; reading byte
 * by byte also keeps clear of unaligned 16-bit loads, since a frame can
 * start at any address. (Marked unused only so the stub builds warning-free.) */
static inline __attribute__((unused)) uint16_t rnlab_l01_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline __attribute__((unused)) void rnlab_l01_copy(uint8_t* dst, const uint8_t* src, size_t n) {
    for(size_t i = 0; i < n; i++) dst[i] = src[i];
}

bool rnlab_decode_frame(const uint8_t* frame, size_t len, rnlab_frame_info_t* info) {
    if(info == NULL) return false;
    *info = (rnlab_frame_info_t){0};
    info->status = RNLAB_L01_ERR_NOT_DECODED;
    info->frame_len = len > 0xFFFFu ? 0xFFFFu : (uint16_t)len;
    (void)frame;

    /* TODO(L01): decode the frame layer by layer and fill `info`.
     *
     *  1. Ethernet II (OSI 2): fewer than 14 B -> RNLAB_L01_ERR_TRUNC_ETH.
     *     Destination MAC, source MAC, EtherType (big-endian);
     *     eth_hdr_len = net_off = 14.
     *  2. EtherType 0x0806 ARP: net = RNLAB_L01_NET_ARP, the whole 28-byte
     *     packet is its header (arp_oper from bytes 6..7 of it), no payload,
     *     the rest of the frame is padding. Cut off -> RNLAB_L01_ERR_TRUNC_NET.
     *     Any EtherType but 0x0800/0x0806 -> RNLAB_L01_NET_OTHER, everything
     *     behind the Ethernet header is payload.
     *  3. EtherType 0x0800 IPv4 (OSI 3): version (4), IHL (header length in
     *     32-bit words, at least 5), total length, TTL, protocol, source and
     *     destination address, fragment bits (MF / offset).
     *     Invalid -> RNLAB_L01_ERR_BAD_NET, cut off -> RNLAB_L01_ERR_TRUNC_NET.
     *     The IPv4 total length - not the frame length - says where the
     *     packet ends; bytes behind it are Ethernet padding (pad_len).
     *  4. Protocol 1 ICMP (8 B header: type, code), 17 UDP (8 B: ports,
     *     length >= 8), 6 TCP (data offset * 4 >= 20 B: ports, flags).
     *     Cut off -> RNLAB_L01_ERR_TRUNC_UPPER, invalid -> RNLAB_L01_ERR_BAD_UPPER;
     *     any other protocol -> RNLAB_L01_UPPER_OTHER (all of it is payload);
     *     a fragment with offset != 0 has no transport header at all.
     *  5. payload_off/payload_len: what is left of the IPv4 packet. If the
     *     total length reaches past the frame -> RNLAB_L01_ERR_TRUNC_PAYLOAD.
     *
     * Never read outside frame[0 .. len-1]; fill in every layer you managed
     * to decode even if a later one fails, and return
     * info->status == RNLAB_L01_OK. The tests in tests/unit/test_rnlab_l01.c
     * use real frames captured between a Mac and the board. */
    return false;
}

/* part / whole in 0.01 % (basis points), rounded to nearest. */
static inline __attribute__((unused)) uint32_t rnlab_l01_ratio_bp(uint32_t part, uint32_t whole) {
    if(whole == 0u) return 0u;
    return (uint32_t)(((uint64_t)part * 10000u + whole / 2u) / whole);
}

bool rnlab_l01_overhead(const rnlab_frame_info_t* info, rnlab_l01_overhead_t* out) {
    if(out == NULL) return false;
    *out = (rnlab_l01_overhead_t){0};
    (void)info;

    /* TODO(L01): from a decoded frame (info->eth_hdr_len != 0, else return
     * false) compute
     *   payload  = the payload of the top decoded layer,
     *   headers  = Ethernet + IPv4/ARP + ICMP/UDP/TCP header bytes,
     *   padding  = padding already in the frame plus what the MAC adds to
     *              reach 60 B (a frame from the TX hook is not padded yet),
     *   frame    = frame length incl. padding and 4 B FCS (at least 64 B),
     *   wire     = frame + 8 B preamble/SFD + 12 B inter-frame gap,
     * and both efficiencies in 0.01 % with rnlab_l01_ratio_bp(). */
    return false;
}

void rnlab_l01_ping_overhead(uint32_t icmp_payload, rnlab_l01_overhead_t* out) {
    const uint32_t headers = RNLAB_L01_ETH_HDR_LEN + RNLAB_L01_IPV4_MIN_HDR + RNLAB_L01_ICMP_HDR_LEN;
    if(icmp_payload > 0xFFFFu - headers) icmp_payload = 0xFFFFu - headers;

    rnlab_frame_info_t info = {0};
    info.status = RNLAB_L01_OK;
    info.frame_len = (uint16_t)(headers + icmp_payload);
    info.eth_hdr_len = RNLAB_L01_ETH_HDR_LEN;
    info.net = RNLAB_L01_NET_IPV4;
    info.net_off = RNLAB_L01_ETH_HDR_LEN;
    info.net_hdr_len = RNLAB_L01_IPV4_MIN_HDR;
    info.ip_proto = RNLAB_L01_IPPROTO_ICMP;
    info.ip_total_len = (uint16_t)(RNLAB_L01_IPV4_MIN_HDR + RNLAB_L01_ICMP_HDR_LEN + icmp_payload);
    info.upper = RNLAB_L01_UPPER_ICMP;
    info.upper_off = RNLAB_L01_ETH_HDR_LEN + RNLAB_L01_IPV4_MIN_HDR;
    info.upper_hdr_len = RNLAB_L01_ICMP_HDR_LEN;
    info.icmp_type = 8u;
    info.payload_off = (uint16_t)headers;
    info.payload_len = (uint16_t)icmp_payload;
    (void)rnlab_l01_overhead(&info, out);
}

size_t rnlab_l01_format_percent(char* out, size_t size, uint32_t basis_points) {
    char digits[12];
    size_t n = 0;
    uint32_t whole = basis_points / 100u;
    do {
        digits[n++] = (char)('0' + whole % 10u);
        whole /= 10u;
    } while(whole != 0u && n < sizeof(digits));

    size_t pos = 0;
    /* digits (reversed) + ",NN %" + NUL */
    if(out == NULL || size < n + 6u) {
        if(out != NULL && size > 0u) out[0] = '\0';
        return 0;
    }
    while(n > 0u) out[pos++] = digits[--n];
    out[pos++] = ','; /* German decimal comma, like the lab pages */
    out[pos++] = (char)('0' + (basis_points / 10u) % 10u);
    out[pos++] = (char)('0' + basis_points % 10u);
    out[pos++] = ' ';
    out[pos++] = '%';
    out[pos] = '\0';
    return pos;
}

const char* rnlab_l01_net_name(rnlab_l01_net_t net) {
    switch(net) {
    case RNLAB_L01_NET_IPV4: return "IPv4";
    case RNLAB_L01_NET_ARP: return "ARP";
    case RNLAB_L01_NET_OTHER: return "anderes";
    default: return "-";
    }
}

const char* rnlab_l01_upper_name(rnlab_l01_upper_t upper) {
    switch(upper) {
    case RNLAB_L01_UPPER_ICMP: return "ICMP";
    case RNLAB_L01_UPPER_UDP: return "UDP";
    case RNLAB_L01_UPPER_TCP: return "TCP";
    case RNLAB_L01_UPPER_OTHER: return "anderes";
    default: return "-";
    }
}

const char* rnlab_l01_icmp_type_name(uint8_t type) {
    switch(type) {
    case 0u: return "Echo Reply";
    case 3u: return "Destination Unreachable";
    case 5u: return "Redirect";
    case 8u: return "Echo Request";
    case 11u: return "Time Exceeded";
    default: return "?";
    }
}

const char* rnlab_l01_status_text(rnlab_l01_status_t status) {
    switch(status) {
    case RNLAB_L01_OK: return "ok";
    case RNLAB_L01_ERR_NOT_DECODED: return "nicht dekodiert";
    case RNLAB_L01_ERR_TRUNC_ETH: return "Ethernet-Header abgeschnitten";
    case RNLAB_L01_ERR_TRUNC_NET: return "IPv4-/ARP-Header abgeschnitten";
    case RNLAB_L01_ERR_BAD_NET: return "IPv4-Header ungueltig";
    case RNLAB_L01_ERR_TRUNC_UPPER: return "ICMP/UDP/TCP-Header abgeschnitten";
    case RNLAB_L01_ERR_BAD_UPPER: return "UDP/TCP-Header ungueltig";
    case RNLAB_L01_ERR_TRUNC_PAYLOAD: return "Nutzdaten abgeschnitten";
    default: return "?";
    }
}
