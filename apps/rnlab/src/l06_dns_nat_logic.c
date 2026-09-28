/*
 * CaDS Zero - rnlab L06 (DNS und NAT): pure logic. See l06_dns_nat_logic.h.
 */

#include "l06_dns_nat_logic.h"

#define DNS_FLAG_QR 0x8000u

static inline uint16_t get_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t get_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

rnlab_dns_status_t rnlab_l06_read_name(const uint8_t* msg, size_t len, size_t offset, char* out,
                                       size_t out_size, size_t* next) {
    /* TODO(L06): Namen ab `offset` lesen (Aufbau: l06_dns_nat_logic.h).
     *  - Label: Laengenbyte 1..63, dann die Zeichen; mit '.' verbinden.
     *  - 0x00 beendet den Namen.
     *  - 11xxxxxx xxxxxxxx ist ein Kompressionszeiger: weiter bei Offset
     *    (14 Bit). *next zeigt hinter den ERSTEN Zeiger, sonst hinter die 0.
     *  - 01xxxxxx / 10xxxxxx sind reserviert -> RNLAB_DNS_ERR_NAME.
     *  - Nie ausserhalb msg[0..len-1] lesen (-> RNLAB_DNS_ERR_TRUNCATED),
     *    nie ueber out_size schreiben (-> RNLAB_DNS_ERR_NAME_LONG).
     *  - Terminieren bei JEDER Eingabe: ein Zeiger ist nur gueltig, wenn er
     *    vor den Anfang der gerade gelesenen Labelfolge zeigt. */
    (void)msg;
    (void)len;
    (void)offset;
    (void)out;
    (void)out_size;
    (void)next;
    return RNLAB_DNS_ERR_TODO;
}

rnlab_dns_status_t rnlab_l06_parse(const uint8_t* msg, size_t len, rnlab_dns_reply_t* out) {
    /* TODO(L06): DNS-Antwort zerlegen.
     *  1. Header (12 Byte): id, flags, rcode = flags & 0x0F, ancount.
     *     QR-Bit (0x8000) muss gesetzt sein, qdcount genau 1.
     *  2. Frage: Name mit rnlab_l06_read_name() nach out->qname, dann
     *     type (2) und class (2).
     *  3. ancount Antworten: Name (nur ueberspringen), type, class, ttl,
     *     rdlength, rdata. Fuer A/IN muss rdlength 4 sein -> addr.
     *     Hoechstens RNLAB_DNS_ANSWERS_MAX speichern, aber alle ueberlesen.
     *     TTL mit gesetztem obersten Bit gilt als 0 (RFC 2181 §8).
     *  4. Jedes Feld vor dem Lesen gegen len pruefen. */
    (void)get_be32; /* liest 4 Byte Big Endian - fuer dich */
    (void)msg;
    (void)len;
    (void)out;
    return RNLAB_DNS_ERR_TODO;
}

/* --- provided (not part of the exercise) ---------------------------------- */

bool rnlab_l06_min_ttl(const rnlab_dns_reply_t* reply, uint32_t* ttl) {
    bool found = false;
    uint32_t min = 0u;
    for(uint8_t i = 0u; reply && i < reply->n_answers; i++) {
        if(reply->answers[i].type != RNLAB_DNS_TYPE_A) continue;
        if(!found || reply->answers[i].ttl < min) min = reply->answers[i].ttl;
        found = true;
    }
    if(found && ttl) *ttl = min;
    return found;
}

bool rnlab_l06_find_dns(const uint8_t* frame, size_t len, const uint8_t** msg, size_t* msg_len,
                        uint16_t* sport, uint16_t* dport) {
    if(!frame || !msg || !msg_len || len < 14u + 20u + 8u) return false;
    if(get_be16(&frame[12]) != 0x0800u) return false;

    const uint8_t* ip = &frame[14];
    size_t ip_avail = len - 14u;
    size_t ihl = (size_t)(ip[0] & 0x0Fu) * 4u;
    if((ip[0] >> 4) != 4u || ihl < 20u || ip_avail < ihl + 8u) return false;
    if(ip[9] != 17u) return false;                       /* UDP */
    if((get_be16(&ip[6]) & 0x3FFFu) != 0u) return false; /* fragment */

    const uint8_t* udp = ip + ihl;
    uint16_t s = get_be16(&udp[0]);
    uint16_t d = get_be16(&udp[2]);
    uint16_t ulen = get_be16(&udp[4]);
    if(s != RNLAB_DNS_PORT && d != RNLAB_DNS_PORT) return false;
    if(ulen < 8u || ulen > ip_avail - ihl) return false;

    *msg = udp + 8u;
    *msg_len = (size_t)ulen - 8u;
    if(sport) *sport = s;
    if(dport) *dport = d;
    return true;
}

const char* rnlab_l06_status_name(rnlab_dns_status_t status) {
    switch(status) {
        case RNLAB_DNS_OK:
            return "ok";
        case RNLAB_DNS_ERR_TRUNCATED:
            return "abgeschnitten";
        case RNLAB_DNS_ERR_NOT_RESPONSE:
            return "keine Antwort (QR=0)";
        case RNLAB_DNS_ERR_NAME:
            return "Name fehlerhaft";
        case RNLAB_DNS_ERR_NAME_LONG:
            return "Name zu lang";
        case RNLAB_DNS_ERR_FORMAT:
            return "Formatfehler";
        case RNLAB_DNS_ERR_TODO:
            return "TODO(L06) offen";
        default:
            return "?";
    }
}

const char* rnlab_l06_type_name(uint16_t type) {
    switch(type) {
        case RNLAB_DNS_TYPE_A:
            return "A";
        case RNLAB_DNS_TYPE_CNAME:
            return "CNAME";
        case RNLAB_DNS_TYPE_AAAA:
            return "AAAA";
        default:
            return "?";
    }
}
