/*
 * CaDS Zero toolbox - TAP (Test Anything Protocol) writer.
 *
 * "The screen looked right" is not a gate anyone can run in CI; `ok 4 - canvas
 * flush` is. The bring-up self test streams TAP over the board's UART and
 * scripts/board_test.py exits non-zero on any `not ok`, so the same firmware
 * that a human watches on a panel is also a machine-checkable result.
 *
 * Output goes through a caller-supplied callback rather than to a fixed sink,
 * which is what lets one test body run on the board over USART3 and on the
 * host over stdout without a single #ifdef.
 *
 * Lines end CRLF. That is not cosmetic: the stream is read from a serial
 * terminal where a bare LF leaves the carriage where it was, and the parser in
 * scripts/board_test.py was written against this exact format.
 */

#ifndef CADS_TOOLBOX_TAP_H
#define CADS_TOOLBOX_TAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where a line goes. `text` is not NUL terminated; `length` is authoritative. */
typedef void (*cads_tap_write_t)(void* context, const char* text, size_t length);

/** Treat every field as private; the accessors below are the interface. */
typedef struct {
    cads_tap_write_t write;
    void* context;
    uint32_t number;   /**< assertions emitted so far        */
    uint32_t failures; /**< how many of them said "not ok"   */
    uint32_t planned;  /**< from cads_tap_plan(), 0 if none  */
} cads_tap_t;

/** Bind a writer. Safe to call with a NULL writer, which makes every emitter a
 *  no-op while still counting - useful when a test body doubles as a
 *  self-check that nobody is listening to. */
void cads_tap_init(cads_tap_t* tap, cads_tap_write_t write, void* context);

/** Announce the assertion count up front: "1..n".
 *
 *  Emitting the plan first is what catches a run that dies half way through -
 *  a firmware that resets after test 3 of 10 would otherwise look green. */
void cads_tap_plan(cads_tap_t* tap, uint32_t count);

void cads_tap_ok(cads_tap_t* tap, const char* description);
void cads_tap_not_ok(cads_tap_t* tap, const char* description);

/** Emit ok or not ok according to `passed`, and return `passed` so a caller can
 *  write `if(!cads_tap_check(...)) return;`. */
bool cads_tap_check(cads_tap_t* tap, bool passed, const char* description);

/** A free-form comment line: "# text". Never counts as an assertion. */
void cads_tap_diag(cads_tap_t* tap, const char* text);

/** A machine-readable measurement: "# key: value". */
void cads_tap_diag_uint(cads_tap_t* tap, const char* key, uint32_t value);

/**
 * Emit the summary ("# 9/10 passed" then "# RESULT: PASS" or FAIL) and report
 * whether the run passed.
 *
 * A run also fails when a plan was announced and a different number of
 * assertions arrived, since that is the signature of a test that died early.
 */
bool cads_tap_finish(cads_tap_t* tap);

uint32_t cads_tap_count(const cads_tap_t* tap);
uint32_t cads_tap_failures(const cads_tap_t* tap);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_TAP_H */
