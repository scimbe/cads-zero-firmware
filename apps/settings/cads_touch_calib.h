/*
 * CaDS Zero - interactive touch calibration.
 *
 * The XPT2046 reports raw ADC counts; hal_touch.c maps them to pixels
 * through a per-axis min/max range (cads_hal_touch_set_calibration). The
 * defaults are nominal for this panel module - good enough to use, but a
 * degree or two of physical mounting variance shifts where a tap lands.
 * This view lets the user set the range from their own panel: tap two
 * on-screen crosshairs at known corners, and the raw counts read there
 * become the calibration, persisted so it survives a reboot.
 *
 * Board only in substance (the raw reads and the calibration setter are
 * hal_touch.c's, not part of the portable core/cads_hal.h surface); on the
 * host the view still exists and registers, but says calibration is not
 * meaningful without a real panel - the same honest-stub approach as
 * apps/netiperf's simulator half.
 */

#ifndef CADS_TOUCH_CALIB_H
#define CADS_TOUCH_CALIB_H

#include <stdint.h>

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_TOUCH_CALIB 0x0302u

/** Register the calibration view. Called from cads_settings_init(). */
void cads_touch_calib_init(cads_view_dispatcher_t* dispatcher);

/**
 * Load a saved calibration from storage (cads/storage kv) and apply it to
 * the touch driver, if one was ever saved. No-op when none exists (the
 * driver keeps its built-in defaults) or on the host. Call once at startup,
 * after storage is up - see apps/bringup.
 */
void cads_touch_calib_load(void);

/**
 * Poll for a corner tap while the calibration view is active. Call from the
 * app-tree loop alongside the other per-frame ticks; a no-op otherwise.
 */
void cads_touch_calib_tick(uint32_t now_ms);

#endif /* CADS_TOUCH_CALIB_H */
