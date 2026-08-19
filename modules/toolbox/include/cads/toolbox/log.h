/*
 * CaDS Zero toolbox - leveled logging, routed to the console.
 *
 * The explorer console (apps/bringup/explorer.c) already has plenty of ad hoc
 * "cads_probe_puts(...)" calls scattered through every HAL diagnostic. This
 * is not a replacement for those - they are deliberately verbose,
 * interactive command output. This is for the other case: a line a module
 * wants to say on its own, unprompted, that a caller might want to turn down
 * without recompiling once the console fills up with routine chatter.
 *
 * SAME SINK PATTERN AS cads_tap.h, FOR THE SAME REASON
 * ------------------------------------------------------
 * Output goes through a caller-supplied callback rather than a fixed sink, so
 * the same log call routes to the board's UART or the host's stdout without
 * an #ifdef. Unlike cads_tap_t, which is deliberately a fresh instance per
 * test run, the sink here is one global: a log line has no natural "handle"
 * to be threaded through every function that might want to say something,
 * and forcing one through every call site is what would keep this from
 * actually getting used. cads_log_init() is expected to run once, early,
 * from the same place that wires up the console in the first place.
 *
 * NOT THREAD SAFE - AND NEITHER IS WHAT IT WRITES TO
 * ----------------------------------------------------
 * cads_hal_console_write() is a blocking, unbuffered byte loop with no
 * locking of its own (targets/itsboard/hal/hal_console.c) - two tasks
 * writing to it at once already interleave their bytes today, with or
 * without this module. cads_log() does not add or remove that risk; it is
 * documented here rather than solved here because a lock would mean reaching
 * up to modules/kernel's cads_mutex, and this module sits below it in the
 * dependency graph on purpose (see modules/toolbox/README.md). A caller
 * logging from more than one FreeRTOS task serializes its own access, same
 * as everywhere else in this toolbox that says so.
 *
 * NO TIMESTAMPS HERE
 * -------------------
 * Attaching one would mean calling a HAL clock function, which this module
 * does not have access to and should not gain just for this - a caller that
 * wants one already has cads_hal_ticks_ms() and can put it in the message.
 *
 * NO FORMAT STRINGS
 * -------------------
 * Same reason as the rest of this toolbox (cads/toolbox/fmt.h): no printf
 * anywhere, so build `message` first with cads_str_append()/cads_fmt_uint()
 * and pass the finished line.
 */

#ifndef CADS_TOOLBOX_LOG_H
#define CADS_TOOLBOX_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ordered by severity, least to most verbose - CadsLogError is 0. A message
 * is emitted when its level is at or below the configured minimum:
 * cads_log_set_level(CadsLogWarn) shows CadsLogError and CadsLogWarn, drops
 * CadsLogInfo and CadsLogDebug. The same ordering syslog uses, for anyone who
 * already carries that convention in their head.
 */
typedef enum {
    CadsLogError = 0,
    CadsLogWarn,
    CadsLogInfo,
    CadsLogDebug,
} cads_log_level_t;

/** Same shape as cads_tap_write_t: `text` is not NUL terminated, `length` is
 *  authoritative. */
typedef void (*cads_log_write_t)(void* context, const char* text, size_t length);

/**
 * Configure the global sink and the minimum level that reaches it.
 *
 * Safe to call again to reconfigure, and safe to call with `write` NULL,
 * which makes cads_log() a no-op - useful before the console exists yet, or
 * to silence logging entirely without touching every call site.
 */
void cads_log_init(cads_log_write_t write, void* context, cads_log_level_t minimum);

/** Change the minimum level without touching the sink. */
void cads_log_set_level(cads_log_level_t minimum);

cads_log_level_t cads_log_level(void);

/** One line: "L [tag] message\r\n", L one of E/W/I/D. Silently dropped (not
 *  even counted) when `level` is below the configured minimum, or when no
 *  sink has been configured. `tag` and `message` may be NULL, treated as "". */
void cads_log(cads_log_level_t level, const char* tag, const char* message);

void cads_log_error(const char* tag, const char* message);
void cads_log_warn(const char* tag, const char* message);
void cads_log_info(const char* tag, const char* message);
void cads_log_debug(const char* tag, const char* message);

/** "E", "W", "I" or "D" - what cads_log() itself would print, exposed for a
 *  caller building its own diagnostic output in the same style. */
const char* cads_log_level_letter(cads_log_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_LOG_H */
