/*
 * CaDS Zero - the desktop: home screen and Leo the lion's residence.
 */

#ifndef CADS_DESKTOP_H
#define CADS_DESKTOP_H

#include <stdint.h>

#include "cads_view_dispatcher.h"

/** Registered under this id. The maintainer's boot sequence should call every
 *  app's _init() and then push this as the navigation root. */
#define CADS_VIEW_ID_DESKTOP 0x0100u

/** Register the desktop's view with the dispatcher. */
void cads_desktop_init(cads_view_dispatcher_t* dispatcher);

/**
 * Advance Leo's blink clock and mood.
 *
 * cads_view_t (cads_view.h) has draw/input/enter/exit and nothing else - no
 * periodic callback, because nothing before the desktop needed the passage of
 * time on its own. Blinking does. Call this once per main loop iteration, the
 * same way cads_input_tick() and cads_gui_tick() already are; it is a no-op
 * until cads_desktop_init() has run and cheap (a few comparisons) whenever the
 * desktop is not the view on screen.
 */
void cads_desktop_tick(uint32_t now_ms);

/**
 * Told whenever something launches an app, so Leo's level reflects actual use
 * rather than uptime alone. apps/menu calls this from its activation handler.
 * A no-op before cads_desktop_init().
 */
void cads_desktop_notify_app_opened(void);

#endif /* CADS_DESKTOP_H */
