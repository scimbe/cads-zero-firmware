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

#endif /* CADS_SPLASH_H */
