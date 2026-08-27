/*
 * CaDS Zero - "light organ": a Knight-Rider-style sweep across the adapter's
 * OUT0..OUT15 outputs (D8..D23 on the board's own LED banks - see
 * docs/reference/datasheets/ITSBRD-schematic-Jaehnichen-HAW-rev02.pdf's
 * "GPIOs" sheet: OUT0..7 drive D8..D15 via U2, OUT8..15 drive D16..D23 via
 * U3) whenever the WiFi/Marauder co-processor link is active. Purely
 * cosmetic - a visible "something is talking to the ESP32 right now" tell -
 * and costs nothing when the link is idle (one comparison, no LEDs driven).
 *
 * SHARED RESOURCE: apps/gpio also drives cads_hal_adapter_outputs() (its own
 * manual OUT0..15 control panel). This tick runs unconditionally regardless
 * of which view is on screen, so a Marauder session left running in the
 * background while the GPIO app is open will fight it for the same 16
 * outputs. No view-aware suppression is built for this - it would need
 * either a new cross-module dependency (this file would have to know the
 * GPIO app's view is current) or RAM this firmware does not have to spare,
 * for a conflict that is easy to just not create: don't drive the GPIO
 * app's outputs while a Marauder tool is active, the same "these two things
 * share a resource, use one at a time" discipline CLAUDE.md's CN8/RS232
 * sharing note already documents for this same wiring one layer down. */
#ifndef CADS_LIGHTORGAN_H
#define CADS_LIGHTORGAN_H

#include <stdint.h>

/** Call from the app-tree main loop every tick. Advances the sweep one step
 *  at a time while cads_marauder_link_active() is true; the instant it goes
 *  false, clears all 16 outputs once and goes idle (no further HAL writes)
 *  until activity resumes. */
void cads_lightorgan_tick(uint32_t now_ms);

#endif /* CADS_LIGHTORGAN_H */
