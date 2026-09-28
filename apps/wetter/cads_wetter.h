/*
 * CaDS Zero - weather station app (lab L11): current weather from
 * open-meteo (or a local test server), refreshed cyclically.
 *
 * The refresh policy and the display strings are pure logic in
 * apps/rnlab/src/l11_wetter_app_logic.c (host-tested, shared with
 * `lab 11`); the HTTP client is the one of lab L10. This app is the glue:
 * it runs the controller while its view is open and redraws only what
 * changed - every blit stops the Ethernet receiver (PA7,
 * docs/explanation/pa7-conflict.md).
 */

#ifndef CADS_WETTER_H
#define CADS_WETTER_H

#include <stdint.h>

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_WETTER 0x0D00u

void cads_wetter_init(cads_view_dispatcher_t* dispatcher);

/** Call every pass of the app-tree loop (apps/bringup/explorer_app_demo.c),
 *  like cads_nettools_tick(). A no-op while the view is not open. */
void cads_wetter_tick(uint32_t now_ms);

#endif /* CADS_WETTER_H */
