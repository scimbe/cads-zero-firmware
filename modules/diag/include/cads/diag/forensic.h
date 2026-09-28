/*
 * CaDS Zero - crash forensics ring buffer.
 *
 * The watchdog (core/cads_hal.h's cads_hal_watchdog_init/feed) turns a
 * lockup into a recovered board; this turns it into a recovered board that
 * can also say what happened. Every record here is written by a fault
 * handler or cads_hal_panic() in the instant before the board halts or
 * (once the watchdog stops being fed) resets, so the storage backing this
 * ring MUST survive a warm reset - see the .c file for exactly where it
 * lives and why that is safe.
 *
 * Nothing here is FreeRTOS-aware and nothing here allocates: cads_forensic_
 * record() has to be safe to call from HardFault, from a stack-overflow
 * hook running with corrupted stack state nearby, or from any other context
 * where taking a mutex or calling malloc would make things worse, not
 * better.
 */

#ifndef CADS_DIAG_FORENSIC_H
#define CADS_DIAG_FORENSIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many past crashes the ring remembers. Sized to fit comfortably in a
 * few hundred bytes - see the .c file's placement for why that is cheap
 * here and would not be in ordinary RAM. */
#define CADS_FORENSIC_RING_DEPTH 6u

/** The Cortex-M4 exception frame, in the order the core pushes it
 * automatically (PM0214 2.3.7). Mirrors fault_handlers.c's own local
 * cads_fault_frame_t - duplicated rather than shared because that type is
 * intentionally private to the naked-trampoline implementation detail in
 * that file, and this module has no business depending on it. */
typedef struct {
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
} cads_forensic_frame_t;

typedef struct {
    uint32_t sequence;    /**< Monotonic across the ring's lifetime; higher = more recent. */
    const char* reason;   /**< Static string: "HardFault", "MemManage", a cads_hal_panic() reason, etc. */
    uint32_t uptime_ms;   /**< cads_hal_ticks_ms() at the moment of the record. */
    bool has_frame;       /**< False for a panic() call with no exception frame available. */
    cads_forensic_frame_t frame;
    uint32_t cfsr;
    uint32_t hfsr;
    bool mmfar_valid;
    uint32_t mmfar;
    bool bfar_valid;
    uint32_t bfar;
    uint32_t msp;         /**< MSP at capture. From a task fault this is the ISR stack; compare against the 4K CCM main stack. */
    uint32_t psp;         /**< PSP at capture; the faulting/panicking task's own stack pointer. */
} cads_forensic_record_t;

/**
 * Records one crash. Safe to call from any exception priority or from a
 * corrupted-stack context: touches only static storage, no locks, no
 * allocation. `frame` may be NULL (a plain cads_hal_panic() call has none).
 */
void cads_forensic_record(
    const char* reason,
    const cads_forensic_frame_t* frame,
    uint32_t cfsr,
    uint32_t hfsr,
    bool mmfar_valid,
    uint32_t mmfar,
    bool bfar_valid,
    uint32_t bfar,
    uint32_t msp,
    uint32_t psp);

/** How many valid records are currently in the ring, 0..CADS_FORENSIC_RING_DEPTH. */
uint32_t cads_forensic_count(void);

/**
 * Reads back one record by recency: index 0 is the most recent, 1 the next
 * most recent, and so on up to cads_forensic_count() - 1. Returns false for
 * an out-of-range index (including an empty ring); `out` is left untouched
 * in that case.
 */
bool cads_forensic_get(uint32_t index, cads_forensic_record_t* out);

/**
 * Empties the ring (invalidates every slot's magic). The ring survives warm
 * resets on purpose, so before a measurement the operator needs a way to
 * start from zero - otherwise an old record (or one from an image with a
 * different CCM layout, whose reason pointers no longer point at its
 * strings) is indistinguishable from a new one. Not for fault context.
 */
void cads_forensic_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* CADS_DIAG_FORENSIC_H */
