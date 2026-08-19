/*
 * CaDS Zero toolbox - leveled logging.
 *
 * Module-static sink configuration rather than a caller-owned struct, unlike
 * every other file in this directory - see the header for why a log line
 * needs a global rather than a threaded-through handle. It is configuration,
 * set once and read many times, not per-call scratch, so it does not violate
 * the "no shared scratch between calls" rule the rest of the toolbox follows
 * (that rule is about avoiding a race on a buffer two calls both write to;
 * this is a pointer nothing here ever mutates after cads_log_init()).
 */

#include "cads/toolbox/log.h"

static cads_log_write_t s_write = NULL;
static void* s_context = NULL;
static cads_log_level_t s_minimum = CadsLogInfo;

void cads_log_init(cads_log_write_t write, void* context, cads_log_level_t minimum) {
    s_write = write;
    s_context = context;
    s_minimum = minimum;
}

void cads_log_set_level(cads_log_level_t minimum) {
    s_minimum = minimum;
}

cads_log_level_t cads_log_level(void) {
    return s_minimum;
}

const char* cads_log_level_letter(cads_log_level_t level) {
    switch(level) {
    case CadsLogError: return "E";
    case CadsLogWarn: return "W";
    case CadsLogInfo: return "I";
    case CadsLogDebug: return "D";
    default: return "?";
    }
}

static void cads_log_puts(const char* text) {
    if(!s_write || !text) return;
    size_t length = 0u;
    while(text[length] != '\0') {
        length++;
    }
    if(length == 0u) return;
    s_write(s_context, text, length);
}

void cads_log(cads_log_level_t level, const char* tag, const char* message) {
    if(!s_write) return;
    /* Error(0) < Warn(1) < Info(2) < Debug(3): a smaller number is more
     * severe. Setting the minimum to CadsLogWarn shows Error and Warn
     * (0 and 1, both <= 1) and drops Info and Debug (2 and 3, both > 1). */
    if(level > s_minimum) return;

    cads_log_puts(cads_log_level_letter(level));
    cads_log_puts(" [");
    cads_log_puts(tag ? tag : "");
    cads_log_puts("] ");
    cads_log_puts(message ? message : "");
    cads_log_puts("\r\n");
}

void cads_log_error(const char* tag, const char* message) {
    cads_log(CadsLogError, tag, message);
}

void cads_log_warn(const char* tag, const char* message) {
    cads_log(CadsLogWarn, tag, message);
}

void cads_log_info(const char* tag, const char* message) {
    cads_log(CadsLogInfo, tag, message);
}

void cads_log_debug(const char* tag, const char* message) {
    cads_log(CadsLogDebug, tag, message);
}
