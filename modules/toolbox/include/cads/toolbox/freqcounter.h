/*
 * CaDS Zero toolbox - the period/frequency counter's capture-to-period
 * math, kept free of any timer peripheral register.
 *
 * A hardware input-capture channel hands this two things per edge: the
 * timer's raw counter value at that instant, and whether its own
 * overcapture flag was set (meaning at least one earlier edge was never
 * read before this one arrived - see targets/itsboard/hal/hal_freqcounter.c
 * for what that means on TIM2 specifically). Turning that into a period
 * measurement has two things worth getting right and neither needs real
 * hardware to test:
 *
 *   WRAPAROUND - a free-running counter wraps from its maximum value back
 *   to 0. `capture - last_capture` as unsigned arithmetic gives the
 *   correct delta across exactly one wrap for free (the same technique
 *   this codebase already uses everywhere for `now_ms - start`), so
 *   there is no special-case wrap handling here at all - only a
 *   regression test proving that arithmetic is actually being relied on
 *   correctly rather than assumed.
 *
 *   A MISSED EDGE - if the hardware's overcapture flag is set, an edge
 *   between the last capture this code saw and the one it is looking at
 *   now was lost, so the delta between them no longer means "one
 *   period" - it means "one period plus however many were missed",
 *   silently wrong in a way indistinguishable from a real slow signal.
 *   Reporting that delta as a measurement would be exactly the kind of
 *   unmeasured error this project has repeatedly chosen not to hide
 *   (explorer_sniff_demo.c's three-counter loss model, the WoL task's
 *   byte-perfect MMC cross-check). This resynchronises on the new edge
 *   instead - no period reported for this call, and the gap is counted,
 *   not guessed at.
 *
 * NO HEAP, NO CLOCK OF ITS OWN, for the same reasons cads/toolbox/
 * mactable.h gives: a plain struct, fed by the caller, testable on the
 * host with a scripted sequence of captures instead of a real timer.
 *
 * DUTY CYCLE REUSES THE SAME REFERENCE POINT, NOT A SECOND STATE MACHINE
 * ---------------------------------------------------------------------
 * docs/ROADMAP.md's own wording for the duty-cycle bullet: "same
 * input-capture channel, second capture compare register" - a falling
 * edge on the same physical pin as the rising edges above, captured via
 * the timer's channel-swap (indirect) mapping rather than a second
 * pin (see hal_freqcounter.c's own header for the register-level
 * detail). cads_freqcounter_capture_high() measures the high time as
 * the delta from the most recent RISING edge cads_freqcounter_capture()
 * itself already tracked as `last_capture` - no separate rising-edge
 * bookkeeping needed, and the same wraparound-safe unsigned subtraction
 * applies. A falling edge is meaningless before any rising edge has
 * been seen, or right after ITS OWN overcapture (a different flag than
 * the rising edge's - the two can miss independently) - both cases
 * return false rather than a guess, the same policy
 * cads_freqcounter_capture() already applies to periods.
 */

#ifndef CADS_TOOLBOX_FREQCOUNTER_H
#define CADS_TOOLBOX_FREQCOUNTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* --- private --- */
    uint32_t last_capture;
    bool have_last_capture;
    uint32_t missed_count;
    uint32_t period_count;
    uint32_t min_period_ticks;
    uint32_t max_period_ticks;
    uint64_t sum_period_ticks;

    /* duty cycle - fed via cads_freqcounter_capture_high() */
    uint32_t missed_high_count;
    uint32_t high_count;
    uint32_t min_high_ticks;
    uint32_t max_high_ticks;
    uint64_t sum_high_ticks;
} cads_freqcounter_t;

void cads_freqcounter_init(cads_freqcounter_t* fc);

/**
 * Feed one capture event.
 *
 * Returns true and fills `period_ticks` (the tick delta since the
 * previous capture - divide the timer's own tick rate by this to get
 * Hz) when this capture completed a measurable period. Returns false,
 * leaving `period_ticks` untouched, on the very first capture ever
 * (nothing to measure against yet) or right after `overcaptured` is
 * true (the gap cannot be trusted - see this file's own header).
 * Either way the new capture becomes the reference point for the next
 * call.
 */
bool cads_freqcounter_capture(
    cads_freqcounter_t* fc, uint32_t capture, bool overcaptured, uint32_t* period_ticks);

uint32_t cads_freqcounter_period_count(const cads_freqcounter_t* fc);
uint32_t cads_freqcounter_missed_count(const cads_freqcounter_t* fc);

/** 0 when cads_freqcounter_period_count() is 0 - there is nothing to report yet. */
uint32_t cads_freqcounter_min_period_ticks(const cads_freqcounter_t* fc);
uint32_t cads_freqcounter_max_period_ticks(const cads_freqcounter_t* fc);
uint32_t cads_freqcounter_avg_period_ticks(const cads_freqcounter_t* fc);

/**
 * Feed one falling-edge capture, for duty-cycle measurement - see this
 * file's own header for how it relates to cads_freqcounter_capture().
 *
 * Returns true and fills `high_ticks` (the tick count the signal spent
 * high, before this falling edge) when it can be measured against a
 * trustworthy preceding rising edge. Returns false, leaving
 * `high_ticks` untouched, before any rising edge has been captured yet,
 * or when `overcaptured` is true for this falling edge itself.
 */
bool cads_freqcounter_capture_high(
    cads_freqcounter_t* fc, uint32_t capture, bool overcaptured, uint32_t* high_ticks);

uint32_t cads_freqcounter_high_count(const cads_freqcounter_t* fc);
uint32_t cads_freqcounter_missed_high_count(const cads_freqcounter_t* fc);

/** 0 when cads_freqcounter_high_count() is 0 - there is nothing to report yet. */
uint32_t cads_freqcounter_min_high_ticks(const cads_freqcounter_t* fc);
uint32_t cads_freqcounter_max_high_ticks(const cads_freqcounter_t* fc);
uint32_t cads_freqcounter_avg_high_ticks(const cads_freqcounter_t* fc);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_FREQCOUNTER_H */
