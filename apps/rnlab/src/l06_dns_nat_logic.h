/*
 * CaDS Zero - rnlab L06 (DNS und NAT): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l06.c links this file
 * directly on the host. Board integration lives in l06_dns_nat.c.
 *
 * A DNS message (RFC 1035 §4.1) is a 12-byte header followed by four
 * sections - question, answer, authority, additional:
 *
 *   offset  size  field
 *        0     2  id       copied from the query into the response
 *        2     2  flags    QR(15) opcode(14..11) AA TC RD RA Z(6..4) rcode(3..0)
 *        4     2  qdcount  number of questions (1 in practice)
 *        6     2  ancount  number of answer records
 *        8     2  nscount  authority records
 *       10     2  arcount  additional records
 *       12     -  question: name, type(2), class(2)
 *                 answers:  name, type(2), class(2), ttl(4), rdlength(2), rdata
 *
 * A name is a sequence of labels, each <length byte><bytes>, ended by a
 * zero byte: 3www7example3com0. To save space a name may end in a
 * compression pointer instead (RFC 1035 §4.1.4): two bytes 11pppppp
 * pppppppp, "the rest of the name is at offset p". Length bytes 01xxxxxx
 * and 10xxxxxx are reserved and invalid.
 *
 * Addresses are host byte order, 93.184.215.14 = 0x5DB8D70E, like the
 * rest of the `lab` framework.
 */

#ifndef RNLAB_L06_DNS_NAT_LOGIC_H
#define RNLAB_L06_DNS_NAT_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNLAB_DNS_PORT 53u
#define RNLAB_DNS_HEADER_LEN 12u

/** Longest name this lesson stores, including the terminating NUL. */
#define RNLAB_DNS_NAME_MAX 64u
/** Answer records kept per response; further ones are counted, not stored. */
#define RNLAB_DNS_ANSWERS_MAX 4u

#define RNLAB_DNS_TYPE_A 1u
#define RNLAB_DNS_TYPE_CNAME 5u
#define RNLAB_DNS_TYPE_AAAA 28u
#define RNLAB_DNS_CLASS_IN 1u

typedef enum {
    RNLAB_DNS_OK = 0,
    RNLAB_DNS_ERR_TRUNCATED,    /**< a field runs past the end of the message */
    RNLAB_DNS_ERR_NOT_RESPONSE, /**< QR bit is 0 - that is a query */
    RNLAB_DNS_ERR_NAME,         /**< bad label, pointer loop/forward pointer */
    RNLAB_DNS_ERR_NAME_LONG,    /**< name does not fit into the output buffer */
    RNLAB_DNS_ERR_FORMAT,       /**< qdcount != 1, bad A rdlength, ... */
    RNLAB_DNS_ERR_TODO,         /**< not implemented yet */
} rnlab_dns_status_t;

/** One answer record. `addr` only for A records (type 1, class IN). */
typedef struct {
    uint16_t type;
    uint32_t ttl;  /**< seconds */
    uint32_t addr; /**< IPv4 for type A, else 0 */
} rnlab_dns_answer_t;

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint8_t rcode;                  /**< flags & 0x0F: 0 = NOERROR, 3 = NXDOMAIN, ... */
    uint16_t ancount;               /**< as announced in the header */
    char qname[RNLAB_DNS_NAME_MAX]; /**< question name, dotted, no final dot */
    uint16_t qtype;
    uint8_t n_answers; /**< records stored in answers[], <= RNLAB_DNS_ANSWERS_MAX */
    rnlab_dns_answer_t answers[RNLAB_DNS_ANSWERS_MAX];
} rnlab_dns_reply_t;

/**
 * Read the (possibly compressed) name at `offset` into `out` as dotted text
 * without a final dot ("www.example.com"; the root name gives "").
 * `*next` receives the offset just behind the name *where it was found*,
 * i.e. behind the first pointer if the name is compressed - that is where
 * the record continues. If the name does not fit into `out`, the result is
 * RNLAB_DNS_ERR_NAME_LONG, but `*next` is set all the same: the caller may
 * skip an over-long owner name and keep parsing the record behind it.
 *
 * Must never read outside msg[0 .. len-1] and must terminate on any
 * input: a pointer is only valid if it points strictly before the label it
 * replaces (so every jump goes backwards and a loop is impossible).
 */
rnlab_dns_status_t rnlab_l06_read_name(const uint8_t* msg, size_t len, size_t offset, char* out,
                                       size_t out_size, size_t* next);

/**
 * Parse a DNS response: header, exactly one question, then the answer
 * section. Stores up to RNLAB_DNS_ANSWERS_MAX answers (all types, `addr`
 * set for A/IN records); authority and additional sections are ignored.
 * An A record with rdlength != 4 is RNLAB_DNS_ERR_FORMAT.
 */
rnlab_dns_status_t rnlab_l06_parse(const uint8_t* msg, size_t len, rnlab_dns_reply_t* out);

/**
 * Smallest TTL over the A records of `reply` - how long a resolver may
 * cache the answer as a whole. Returns false if there is no A record.
 * Provided.
 */
bool rnlab_l06_min_ttl(const rnlab_dns_reply_t* reply, uint32_t* ttl);

/**
 * Find a DNS message in a raw Ethernet frame: IPv4, unfragmented UDP,
 * source or destination port 53. Returns the UDP payload plus both ports
 * (the board's source port is what NAT rewrites). Provided.
 */
bool rnlab_l06_find_dns(const uint8_t* frame, size_t len, const uint8_t** msg, size_t* msg_len,
                        uint16_t* sport, uint16_t* dport);

/** Short text for a status ("ok", "abgeschnitten", ...). Provided. */
const char* rnlab_l06_status_name(rnlab_dns_status_t status);

/** "A", "CNAME", "AAAA" or "?". Provided. */
const char* rnlab_l06_type_name(uint16_t type);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L06_DNS_NAT_LOGIC_H */
