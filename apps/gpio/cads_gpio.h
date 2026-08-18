/*
 * CaDS Zero - GPIO: drive OUT0..15, watch IN0..7 and INT0..5 live.
 */

#ifndef CADS_GPIO_H
#define CADS_GPIO_H

#include <stdint.h>

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_GPIO 0x0500u

void cads_gpio_init(cads_view_dispatcher_t* dispatcher);

/**
 * Poll the adapter's input lines and redraw whichever indicator cells
 * changed.
 *
 * IN0..7 and INT0..5 are external signals, not something the input service
 * (services/input) knows about - they never produce an event, so nothing
 * calls this view's draw() on their behalf. Like cads_desktop_tick(), call
 * this once per main loop iteration; it polls at a fixed rate internally and
 * is a no-op whenever GPIO is not the visible view.
 */
void cads_gpio_tick(uint32_t now_ms);

#endif /* CADS_GPIO_H */
