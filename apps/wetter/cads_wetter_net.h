/*
 * CaDS Zero - weather app: the fetch backend, one per target.
 *
 * Board: the asynchronous HTTP client of lab L10 (rnlab_l10_fetch_*).
 * Simulator: no network (cads_net_sim reports no link), so nothing to
 * fetch - but the app, its view and its tests still build and run there.
 */

#ifndef CADS_WETTER_NET_H
#define CADS_WETTER_NET_H

#include <stdbool.h>
#include <stdint.h>

#include "l10_http_wetter_1_logic.h"

/** Start a GET of the open-meteo query on host:port (HTTP/1.0). False when
 *  it could not start; then cads_wetter_net_result() says why. */
bool cads_wetter_net_start(const char* host, uint16_t port);
bool cads_wetter_net_busy(void);
void cads_wetter_net_service(uint32_t now_ms);
void cads_wetter_net_abort(void);
const rnlab_fetch_result_t* cads_wetter_net_result(void);

#endif /* CADS_WETTER_NET_H */
