#include "cads/cli/cli_stream.h"

#include <string.h>

/* RFC 854 command bytes. */
#define TELNET_SE   240u
#define TELNET_SB   250u
#define TELNET_WILL 251u
#define TELNET_DONT 254u
#define TELNET_IAC  255u

enum {
    TelnetData = 0,
    TelnetIac,       /* saw IAC, next byte is the command */
    TelnetOption,    /* saw IAC WILL/WONT/DO/DONT, next byte is the option */
    TelnetSub,       /* inside IAC SB ... */
    TelnetSubIac,    /* inside a subnegotiation, saw IAC */
};

void cads_cli_telnet_init(cads_cli_telnet_t* telnet) {
    if(telnet) telnet->state = TelnetData;
}

bool cads_cli_telnet_filter(cads_cli_telnet_t* telnet, uint8_t byte) {
    switch(telnet->state) {
    case TelnetData:
        if(byte == TELNET_IAC) {
            telnet->state = TelnetIac;
            return false;
        }
        return true;
    case TelnetIac:
        if(byte >= TELNET_WILL && byte <= TELNET_DONT) {
            telnet->state = TelnetOption;
        } else if(byte == TELNET_SB) {
            telnet->state = TelnetSub;
        } else {
            /* IAC IAC (a literal 0xFF, never CLI text) or a two-byte
             * command (NOP, AYT, GA, ...): consumed either way. */
            telnet->state = TelnetData;
        }
        return false;
    case TelnetOption:
        telnet->state = TelnetData;
        return false;
    case TelnetSub:
        if(byte == TELNET_IAC) telnet->state = TelnetSubIac;
        return false;
    default: /* TelnetSubIac */
        telnet->state = (byte == TELNET_SE) ? TelnetData : TelnetSub;
        return false;
    }
}

void cads_cli_outq_init(cads_cli_outq_t* q, char* storage, size_t size) {
    q->data = storage;
    q->size = size;
    q->head = 0u;
    q->count = 0u;
    q->truncated = false;
}

size_t cads_cli_outq_push(cads_cli_outq_t* q, const char* text, size_t length) {
    if(q->size == 0u) {
        if(length) q->truncated = true;
        return 0u;
    }
    size_t room = q->size - q->count;
    size_t take = length < room ? length : room;
    if(take < length) q->truncated = true;

    size_t tail = (q->head + q->count) % q->size;
    size_t first = q->size - tail;
    if(first > take) first = take;
    memcpy(q->data + tail, text, first);
    memcpy(q->data, text + first, take - first);
    q->count += take;
    return take;
}

size_t cads_cli_outq_peek(const cads_cli_outq_t* q, const char** chunk) {
    size_t run = q->size - q->head;
    if(run > q->count) run = q->count;
    *chunk = q->data + q->head;
    return run;
}

void cads_cli_outq_consume(cads_cli_outq_t* q, size_t length) {
    if(length > q->count) length = q->count;
    q->head = (q->head + length) % q->size;
    q->count -= length;
    if(q->count == 0u) q->head = 0u;
}
