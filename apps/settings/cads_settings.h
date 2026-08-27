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
#define CADS_VIEW_ID_TEST_PATTERN     0x0303u  /* Settings -> Test pattern */

/** Register both settings views with the dispatcher. */
void cads_settings_init(cads_view_dispatcher_t* dispatcher);

/**
 * Service a pending "Reload config" request. MUST be called only from the
 * app-tree loop (the console task) - it does the littlefs load the menu row
 * deliberately does NOT do on the input task. A no-op when nothing is pending.
 */
void cads_settings_service_config(void);

#endif /* CADS_SETTINGS_H */
