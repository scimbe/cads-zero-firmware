#include <stdint.h>

#ifndef CADS_SPLASH_H
#define CADS_SPLASH_H

/**
 * Draw the boot screen: the CaDS mark, the firmware name, and a status line.
 *
 * Portable, so the simulator shows the same thing the board does. Draws into
 * the canvas but does not flush - the caller decides when the screen is worth
 * the 230 ms a full-screen transfer costs.
 */
void cads_splash_draw(const char* status);

/**
 * The splash with a progress bar (0..100%) under the wordmark. What the boot
 * animates so a person sees activity, instead of leaving a raw test pattern
 * on the panel. Each call redraws the whole screen.
 */
void cads_splash_draw_progress(const char* status, uint8_t percent);

/**
 * The 480x320 display test pattern - palette swatches, corner markers,
 * diagonals - for eyeballing hue/geometry/scan direction on the write-only
 * panel. Moved out of the boot path (Settings -> Test pattern) so the default
 * boot shows the branded progress screen instead; still full-screen, so a
 * flush of it transfers every pixel.
 */
void cads_test_pattern_draw(void);

#endif /* CADS_SPLASH_H */
