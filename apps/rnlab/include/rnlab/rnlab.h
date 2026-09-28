/*
 * CaDS Zero - computer-networks lab (apps/rnlab), the board-facing entry
 * points apps/bringup calls. Everything lesson-specific lives behind the
 * `lab` CLI command (rnlab_lesson.h), not here.
 *
 * All of these run on the console task - the one task that already owns
 * cads_net_poll() and the serial console - so nothing here needs a lock.
 */

#ifndef RNLAB_H
#define RNLAB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** TCP port of the CLI listener rnlab_init() starts (same as the
 *  explorer's `j` command, so both reach the same listener). */
#define RNLAB_CLI_TCP_PORT 4242u

/**
 * Bring the network up (cads_net_init() with `mac`), register the `lab`
 * command with cads_cli and start the TCP CLI listener. Idempotent. The
 * address is whatever cads_net already defaults to (static
 * 192.168.33.99/24, gateway 192.168.33.1) unless /config.txt or
 * `lab net ...` changes it.
 */
void rnlab_init(const uint8_t mac[6]);

/** Pump the network and run pending TCP CLI commands while the explorer's
 *  command loop is idle - the app tree's own loop does both itself. */
void rnlab_poll(void);

/** Default and bounds of the network poll interval while the app tree
 *  idles (`lab poll <ms>`). 10 ms is what the app tree always did. */
#define RNLAB_POLL_MS_DEFAULT 10u
#define RNLAB_POLL_MS_MIN     1u
#define RNLAB_POLL_MS_MAX     10u

/**
 * Wait `ms` (the app tree's 10 ms tick) without leaving the network alone
 * for all of it: every `lab poll` interval the wait is interrupted for
 * cads_net_poll() and cads_cli_tcp_service(). With the default of 10 ms
 * this is exactly the old single delay; with `lab poll 1` a frame waits at
 * most ~1 ms instead of up to 10 ms (L04 measures that difference in the
 * ping RTT spread).
 */
void rnlab_idle_ms(uint32_t ms);

/** Feed one byte of the serial console into the lab's serial CLI session
 *  (used while the app tree owns the console; plain ASCII there was
 *  ignored before). */
void rnlab_serial_feed(uint8_t byte);

/** If `line` is a `lab ...` command, run it on the serial session and
 *  return true; otherwise return false and leave it to the caller. */
bool rnlab_serial_line(const char* line);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_H */
