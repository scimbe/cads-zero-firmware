/*
 * CaDS Zero - rnlab L10 (HTTP-Client (Wetter 1)): the asynchronous HTTP
 * client on the board, as an API for `lab 10` and for the weather app of
 * L11 (apps/wetter).
 *
 * Board only (lwIP raw API, NO_SYS): implemented in l10_http_wetter_1.c,
 * which the simulator does not build. The result types are plain C and live
 * in l10_http_wetter_1_logic.h, so the host tests and the simulator see
 * them too.
 *
 * WHY ASYNCHRONOUS
 * ----------------
 * A fetch over the internet takes a few hundred milliseconds, a dead server
 * up to RNLAB_L10_TIMEOUT_MS. A `lab` handler or an app tick that waited
 * for it would stop the whole main loop - display, keys, every other
 * connection - for that long. So rnlab_l10_fetch_start() only starts the
 * fetch and returns; DNS, connect, receive and close happen in lwIP
 * callbacks during the normal cads_net_poll() of the main loop, and the
 * caller looks at rnlab_l10_fetch_result() later (`lab 10 show`, or the
 * app's tick).
 */

#ifndef RNLAB_L10_HTTP_WETTER_1_H
#define RNLAB_L10_HTTP_WETTER_1_H

#include <stdbool.h>
#include <stdint.h>

#include "l10_http_wetter_1_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start fetching http://host:port/path. `host` may be a name (resolved by
 * lwIP's DNS client) or a dotted-quad literal (no DNS). Returns false and
 * sets the result to DONE with the reason when it cannot even start (bad
 * argument, no link, no address, no DNS server, out of PCBs) or when a
 * fetch is still running (then the running one is left alone and false is
 * returned without touching the result).
 */
bool rnlab_l10_fetch_start(const char* host, uint16_t port, const char* path, bool http11);

/** True while a fetch is between start and DONE. */
bool rnlab_l10_fetch_busy(void);

/** Enforce RNLAB_L10_TIMEOUT_MS also while DNS is pending (no PCB exists
 *  then to carry a poll timer). Call now and then; cheap. */
void rnlab_l10_fetch_service(uint32_t now_ms);

/** Stop a running fetch (closes/aborts the connection). No-op when idle. */
void rnlab_l10_fetch_abort(void);

/** The current or last fetch. Never NULL. */
const rnlab_fetch_result_t* rnlab_l10_fetch_result(void);

/** The first bytes of the last response's header / body, NUL-terminated,
 *  for `lab 10 head` / `lab 10 body`. */
const char* rnlab_l10_raw_header(void);
const char* rnlab_l10_raw_body(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L10_HTTP_WETTER_1_H */
