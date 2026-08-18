/*
 * CaDS Zero - settings: brightness, SPI clock, touch calibration, factory reset.
 */

#ifndef CADS_SETTINGS_H
#define CADS_SETTINGS_H

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_SETTINGS         0x0300u
/** A confirm/info dialog pushed as its own view - see cads_settings.c for why
 *  it is a separate view rather than a dialog nested inside the list. */
#define CADS_VIEW_ID_SETTINGS_CONFIRM 0x0301u

/** Register both settings views with the dispatcher. */
void cads_settings_init(cads_view_dispatcher_t* dispatcher);

#endif /* CADS_SETTINGS_H */
