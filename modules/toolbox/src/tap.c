/*
 * CaDS Zero toolbox - TAP writer.
 *
 * The output format is fixed by scripts/board_test.py, which parses this
 * stream over a serial line to decide whether a milestone passed. Changing a
 * separator here breaks the hardware gate, so the strings are assembled
 * literally rather than through anything clever.
 */

#include "cads/toolbox/tap.h"

#include "cads/toolbox/fmt.h"

#define CADS_TAP_EOL "\r\n"

static void cads_tap_emit(cads_tap_t* tap, const char* text, size_t length) {
    if(!tap || !tap->write || !text || length == 0u) return;
    tap->write(tap->context, text, length);
}

static void cads_tap_puts(cads_tap_t* tap, const char* text) {
    size_t length = 0u;
    if(!text) return;
    while(text[length] != '\0') {
        length++;
    }
    cads_tap_emit(tap, text, length);
}

static void cads_tap_put_uint(cads_tap_t* tap, uint32_t value) {
    char digits[CADS_FMT_BUFFER];
    size_t length = cads_fmt_uint(digits, sizeof(digits), value);
    cads_tap_emit(tap, digits, length);
}

void cads_tap_init(cads_tap_t* tap, cads_tap_write_t write, void* context) {
    if(!tap) return;
    tap->write = write;
    tap->context = context;
    tap->number = 0u;
    tap->failures = 0u;
    tap->planned = 0u;
}

void cads_tap_plan(cads_tap_t* tap, uint32_t count) {
    if(!tap) return;
    tap->planned = count;
    cads_tap_puts(tap, "1..");
    cads_tap_put_uint(tap, count);
    cads_tap_puts(tap, CADS_TAP_EOL);
}

static void cads_tap_result(cads_tap_t* tap, bool passed, const char* description) {
    if(!tap) return;

    tap->number++;
    if(!passed) tap->failures++;

    cads_tap_puts(tap, passed ? "ok " : "not ok ");
    cads_tap_put_uint(tap, tap->number);
    cads_tap_puts(tap, " - ");
    cads_tap_puts(tap, description);
    cads_tap_puts(tap, CADS_TAP_EOL);
}

void cads_tap_ok(cads_tap_t* tap, const char* description) {
    cads_tap_result(tap, true, description);
}

void cads_tap_not_ok(cads_tap_t* tap, const char* description) {
    cads_tap_result(tap, false, description);
}

bool cads_tap_check(cads_tap_t* tap, bool passed, const char* description) {
    cads_tap_result(tap, passed, description);
    return passed;
}

void cads_tap_diag(cads_tap_t* tap, const char* text) {
    cads_tap_puts(tap, "# ");
    cads_tap_puts(tap, text);
    cads_tap_puts(tap, CADS_TAP_EOL);
}

void cads_tap_diag_uint(cads_tap_t* tap, const char* key, uint32_t value) {
    cads_tap_puts(tap, "# ");
    cads_tap_puts(tap, key);
    cads_tap_puts(tap, ": ");
    cads_tap_put_uint(tap, value);
    cads_tap_puts(tap, CADS_TAP_EOL);
}

bool cads_tap_finish(cads_tap_t* tap) {
    if(!tap) return false;

    bool complete = (tap->planned == 0u) || (tap->planned == tap->number);

    cads_tap_puts(tap, "# ");
    cads_tap_put_uint(tap, tap->number - tap->failures);
    cads_tap_puts(tap, "/");
    cads_tap_put_uint(tap, tap->number);
    cads_tap_puts(tap, " passed" CADS_TAP_EOL);

    if(!complete) {
        /* Say so on its own line: a run that stopped early and a run that
         * failed an assertion need different investigations. */
        cads_tap_puts(tap, "# planned ");
        cads_tap_put_uint(tap, tap->planned);
        cads_tap_puts(tap, ", ran ");
        cads_tap_put_uint(tap, tap->number);
        cads_tap_puts(tap, CADS_TAP_EOL);
    }

    bool passed = (tap->failures == 0u) && complete;
    cads_tap_puts(tap, passed ? "# RESULT: PASS" CADS_TAP_EOL : "# RESULT: FAIL" CADS_TAP_EOL);
    return passed;
}

uint32_t cads_tap_count(const cads_tap_t* tap) {
    return tap ? tap->number : 0u;
}

uint32_t cads_tap_failures(const cads_tap_t* tap) {
    return tap ? tap->failures : 0u;
}
