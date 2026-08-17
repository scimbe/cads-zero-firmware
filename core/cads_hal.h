/*
 * CaDS Zero - hardware abstraction layer.
 *
 * Everything above this header is portable C: it compiles unchanged for the
 * ITSboard and for the host simulator. Two implementations exist:
 *
 *   targets/itsboard/hal/   real STM32F429 peripherals
 *   targets/sim/            SDL2-backed simulation of the whole rig
 *
 * Keeping the surface this narrow is what makes the simulator honest - if a
 * feature is not expressible here, it cannot silently work in only one of the
 * two worlds.
 */

#ifndef CADS_HAL_H
#define CADS_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Panel geometry. Fixed at compile time on both backends. */
#define CADS_DISPLAY_WIDTH  480
#define CADS_DISPLAY_HEIGHT 320

/* --- lifecycle ----------------------------------------------------------- */

/** Clocks, flash latency, core caches. Runs before the C runtime is usable,
 *  so it must not touch initialised data. */
void cads_hal_early_init(void);

/** Everything else: GPIO, timers, console, display, input. */
void cads_hal_init(void);

/* --- time ---------------------------------------------------------------- */

uint32_t cads_hal_ticks_ms(void);
uint64_t cads_hal_ticks_us(void);
void cads_hal_delay_us(uint32_t us);
void cads_hal_delay_ms(uint32_t ms);

/* --- console (USART3 -> ST-Link VCP on hardware, stdout in the simulator) - */

void cads_hal_console_init(uint32_t baud);
void cads_hal_console_write(const void* data, size_t length);

/** Non-blocking single byte read. Returns true when a byte was available. */
bool cads_hal_console_read(uint8_t* byte);

/* --- display ------------------------------------------------------------- */

void cads_hal_display_init(void);

/** Backlight duty in percent, 0..100. */
void cads_hal_display_backlight(uint8_t percent);

/**
 * Select the display bus clock.
 *
 * false = the always-safe divider, true = the faster one. The limit is the
 * shield's 74HC4094 shift register chain rather than the panel, so the fast
 * setting is only ever enabled after being qualified on real hardware.
 * See docs/SAFETY.md, "Raising the SPI clock".
 *
 * No-op in the simulator.
 */
void cads_hal_display_set_fast_clock(bool fast);

/**
 * Push a rectangle of RGB565 pixels to the panel.
 *
 * The buffer must stay valid until cads_hal_display_busy() reports false: on
 * hardware the transfer is handed to DMA and returns immediately.
 * On hardware the source buffer must live in DMA-capable SRAM, never in CCM.
 */
void cads_hal_display_blit(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t* pixels);

/** True while a blit is still in flight. */
bool cads_hal_display_busy(void);

/** Block until any in-flight blit has completed. */
void cads_hal_display_wait(void);

/* --- touch panel (XPT2046) ----------------------------------------------- */

typedef struct {
    uint16_t x;        /**< 0..CADS_DISPLAY_WIDTH-1, only valid when pressed */
    uint16_t y;        /**< 0..CADS_DISPLAY_HEIGHT-1                          */
    uint16_t pressure; /**< raw, larger means firmer contact                  */
    bool pressed;
} cads_touch_state_t;

void cads_hal_touch_read(cads_touch_state_t* state);

/* --- ITS adapter board I/O ----------------------------------------------- */

/** IN0..IN7 on GPIOF, active low (pulled up). Bit n = INn. */
uint8_t cads_hal_adapter_inputs(void);

/** INT0..INT5 on GPIOG, active low. Bit n = INTn. */
uint8_t cads_hal_adapter_interrupts(void);

/** OUT0..OUT15: bits 0..7 go to GPIOD, bits 8..15 to GPIOE. */
void cads_hal_adapter_outputs(uint16_t value);

/* --- on-board indicators -------------------------------------------------- */

typedef enum {
    CadsLedGreen,
    CadsLedBlue,
    CadsLedRed,
} cads_led_t;

void cads_hal_led_set(cads_led_t led, bool on);
void cads_hal_led_toggle(cads_led_t led);

/** The blue USER button on the Nucleo, active high. */
bool cads_hal_user_button(void);

/* --- raw port inspection --------------------------------------------------
 *
 * Used only by the hardware explorer, which exists because the ITS adapter's
 * wiring is not documented anywhere we have. Exposed through the HAL rather
 * than letting a tool in apps/ reach for the device headers directly - the rule
 * that everything above the HAL builds for both targets is worth more than the
 * convenience.
 */

/** Number of inspectable GPIO ports on this target. */
uint32_t cads_hal_port_count(void);

/** Single-letter name of a port, 'A'..'K' on the board. */
char cads_hal_port_name(uint32_t index);

/** Raw input register of a port. */
uint16_t cads_hal_port_read(uint32_t index);

/**
 * True for pins that must not be repurposed: SWD, the HSE input, and the RMII
 * lines. The explorer flags them so a reader is never tempted to wire a button
 * to one. See docs/SAFETY.md.
 */
bool cads_hal_pin_is_reserved(uint32_t port_index, uint32_t pin);

/* --- fault reporting ------------------------------------------------------ */

/**
 * Last-resort failure path: prints the reason on the console, lights the red
 * LED and halts. Never returns. On hardware it issues a breakpoint so an
 * attached debugger stops with the machine state intact.
 */
__attribute__((noreturn)) void cads_hal_panic(const char* reason);

#ifdef __cplusplus
}
#endif

#endif /* CADS_HAL_H */
