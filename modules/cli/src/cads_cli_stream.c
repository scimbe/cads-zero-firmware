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

bool cads_cli_outq_flush_due(const cads_cli_outq_t* q, bool in_command, uint32_t now_ms, uint32_t last_flush_ms) {
    if(q->count == 0u) return false;
    if(!in_command || q->count >= q->size / 2u) return true;
    /* Unsigned difference: correct across the 49.7-day tick wrap. */
    return now_ms - last_flush_ms >= CADS_CLI_PROGRESS_FLUSH_MS;
}

bool cads_cli_input_push(cads_cli_outq_t* in, cads_cli_telnet_t* telnet, const uint8_t* data, size_t length) {
    /* Filtering only ever shrinks the data, so `length` free bytes is
     * enough; checked up front so a refusal leaves the telnet state as it
     * was for the retry. */
    if(in->size - in->count < length) return false;
    for(size_t i = 0; i < length; i++) {
        if(cads_cli_telnet_filter(telnet, data[i])) {
            char c = (char)data[i];
            (void)cads_cli_outq_push(in, &c, 1u);
        }
    }
    return true;
}

size_t cads_cli_input_drain(cads_cli_outq_t* in, cads_cli_session_t* session) {
    size_t fed = 0u;
    /* One byte at a time off the head: a command dispatched by feed() may
     * pump the network and append to this same queue meanwhile. */
    while(in->count > 0u) {
        uint8_t byte = (uint8_t)in->data[in->head];
        cads_cli_outq_consume(in, 1u);
        cads_cli_session_feed(session, byte);
        fed++;
    }
    return fed;
}
