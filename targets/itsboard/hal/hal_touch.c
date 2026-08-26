/*
 * CaDS Zero - XPT2046 resistive touch controller.
 *
 * The XPT2046 shares SPI1 with the panel but needs a far slower clock, so
 * every access brackets itself with a speed change. Readings are noisy by
 * nature: the panel is a resistive divider and the ADC sees the settling
 * curve. Two defences are applied here:
 *
 *   - each axis is sampled several times and the median is taken, which
 *     rejects the single wild sample that averaging would smear in;
 *   - a reading is only reported when the IRQ line is still asserted after
 *     the conversion, so a finger lifting mid-sample cannot emit a phantom
 *     touch at the edge of the panel.
 *
 * Calibration maps raw ADC counts to pixels. The defaults below are nominal
 * for this module; the settings app writes measured values into storage.
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"
#include "hal_spi.h"

/* PD1PD0=00: power-down between conversions, PENIRQ enabled. What
 * cads_touch_pressed() needs to see between reads. */
#define XPT2046_CMD_X 0xD0u /* differential, 12 bit, X position */
#define XPT2046_CMD_Y 0x90u /* differential, 12 bit, Y position */

/* PD1PD0=01: reference off between conversions but the ADC itself stays
 * powered rather than cycling down - and per the datasheet, PD0=1 also
 * disables the PENIRQ comparator while it's set. cads_touch_median() uses
 * these for the actual sampling burst, then explicitly re-arms with one
 * PD=00 command afterward - see cads_touch_rearm_penirq(). Second attempt
 * at the "IRQ correct, coordinates garbage" bug after removing the
 * mid-transaction delay alone was not sufficient (see docs/ROADMAP.md's
 * dated Log entry) - masking PENIRQ during the burst removes a second
 * candidate source of conversion noise the delay fix did not touch. */
#define XPT2046_CMD_X_MASKED 0xD1u
#define XPT2046_CMD_Y_MASKED 0x91u

#define CADS_TOUCH_SAMPLES 5u

typedef struct {
    uint16_t x_min, x_max;
    uint16_t y_min, y_max;
} cads_touch_calibration_t;

static cads_touch_calibration_t cads_touch_calibration = {
    .x_min = 200u,
    .x_max = 3900u,
    .y_min = 200u,
    .y_max = 3900u,
};

static inline void cads_touch_cs(bool active) {
    cads_gpio_write(CADS_PIN_TP_CS_PORT, CADS_PIN_TP_CS, !active);
}

void cads_hal_touch_init(void) {
    cads_gpio_init_output(CADS_PIN_TP_CS_PORT, CADS_PIN_TP_CS, true);
    cads_gpio_init_input(CADS_PIN_TP_IRQ_PORT, CADS_PIN_TP_IRQ, CadsGpioPullUp);
    cads_gpio_init_input(CADS_PIN_TP_BUSY_PORT, CADS_PIN_TP_BUSY, CadsGpioPullUp);
}

/** IRQ is pulled low by the controller while the panel is pressed. */
static bool cads_touch_pressed(void) {
    return !cads_gpio_read(CADS_PIN_TP_IRQ_PORT, CADS_PIN_TP_IRQ);
}

static uint16_t cads_touch_read_axis(uint8_t command) {
    /* No delay between the command byte and the two data bytes: this used
     * to stall 10us here for "conversion time", which is exactly what was
     * breaking every reading. The XPT2046's SAR is a charge-redistribution
     * DAC that is power-down-idle between conversions (PD1PD0=00 in the
     * command byte); stopping the clock mid-transaction for 10us gives
     * that charge time to droop toward zero before the data bytes are even
     * clocked out. Confirmed live on hardware: with the delay, IRQ tracked
     * real touch/release correctly but the 12-bit result was always
     * single/low-double digits (high byte ~0, only the low byte varying) -
     * exactly what a drooped SAR produces. Every known-good XPT2046 driver
     * clocks the command and the two data bytes as one continuous 24-clock
     * burst with no gap; this now does the same. */
    cads_touch_cs(true);
    (void)cads_hal_spi_transfer(command);
    uint8_t high = cads_hal_spi_transfer(0x00u);
    uint8_t low = cads_hal_spi_transfer(0x00u);
    cads_touch_cs(false);

    /* 12 significant bits, left aligned in the 16 bit response. */
    return (uint16_t)(((uint16_t)high << 8 | low) >> 3) & 0x0FFFu;
}

/*
 * Re-arms PENIRQ after a masked (PD1PD0=01) sampling burst: one dummy
 * PD=00 transaction, result discarded. Mandatory, not optional - leaving
 * PENIRQ masked would silently break cads_touch_pressed() for every
 * caller (production reads, the lift-detection check right after
 * sampling, and the raw diagnostics), which is the one thing already
 * confirmed working twice tonight and the last thing to risk breaking
 * with this change. Which axis's command byte is used here does not
 * matter - the result is thrown away - X is picked only for consistency.
 */
static void cads_touch_rearm_penirq(void) {
    (void)cads_touch_read_axis(XPT2046_CMD_X);
}

static uint16_t cads_touch_median(uint8_t masked_command) {
    uint16_t samples[CADS_TOUCH_SAMPLES];
    for(uint32_t i = 0; i < CADS_TOUCH_SAMPLES; i++) {
        samples[i] = cads_touch_read_axis(masked_command);
    }
    cads_touch_rearm_penirq();

    /* Insertion sort: five elements, branch-predictable, no allocation. */
    for(uint32_t i = 1; i < CADS_TOUCH_SAMPLES; i++) {
        uint16_t value = samples[i];
        uint32_t j = i;
        while(j > 0u && samples[j - 1u] > value) {
            samples[j] = samples[j - 1u];
            j--;
        }
        samples[j] = value;
    }
    return samples[CADS_TOUCH_SAMPLES / 2u];
}

static uint16_t cads_touch_scale(uint16_t raw, uint16_t min, uint16_t max, uint16_t span) {
    if(raw <= min) return 0u;
    if(raw >= max) return (uint16_t)(span - 1u);
    return (uint16_t)(((uint32_t)(raw - min) * span) / (max - min));
}

void cads_hal_touch_read(cads_touch_state_t* state) {
    state->pressed = false;
    state->pressure = 0u;
    state->x = 0u;
    state->y = 0u;

    if(!cads_touch_pressed()) return;

    cads_hal_spi_claim_bus();
    cads_hal_spi_set_speed(CadsSpiSpeedTouch);

    uint16_t raw_x = cads_touch_median(XPT2046_CMD_X_MASKED);
    uint16_t raw_y = cads_touch_median(XPT2046_CMD_Y_MASKED);

    cads_hal_spi_restore_display_speed();
    cads_hal_spi_release_bus();

    /* Still pressed after sampling? Otherwise the finger left mid-read and the
     * coordinates are garbage. */
    if(!cads_touch_pressed()) return;
    if(raw_x == 0u || raw_x >= 0x0FFFu) return;

    /* The panel is mounted rotated relative to the controller's axes: the
     * controller's Y runs along the display's X (not mirrored), and the
     * controller's X runs along the display's Y (mirrored). Verified on
     * hardware with the quadrant test pattern: touching the red (top-left)
     * quadrant reads display x<240, y<160, matching where it renders. */
    state->x = cads_touch_scale(
        raw_y, cads_touch_calibration.y_min, cads_touch_calibration.y_max, CADS_DISPLAY_WIDTH);
    state->y = (uint16_t)(CADS_DISPLAY_HEIGHT - 1u -
                          cads_touch_scale(
                              raw_x,
                              cads_touch_calibration.x_min,
                              cads_touch_calibration.x_max,
                              CADS_DISPLAY_HEIGHT));
    state->pressure = 1u;
    state->pressed = true;
}

/*
 * Diagnostic only, not part of the portable cads_hal.h surface: reads the
 * XPT2046's raw ADC counts unconditionally, ignoring cads_touch_pressed()
 * entirely. Exists to answer one question during a live investigation -
 * TP_IRQ (PE13) never toggled in a port-wide watch even while the panel was
 * actively pressed, so is the SPI link to the controller itself alive at
 * all, or is the whole chip unresponsive? If this reports plausible,
 * varying counts while pressed, the SPI/ADC half works and the fault is
 * narrowed to the IRQ line specifically (wiring or the controller's PENIRQ
 * output); if it reports the same fixed junk regardless of touch, the
 * whole link is suspect. Declared extern directly in explorer.c rather than
 * added to core/cads_hal.h - this is not a capability the simulator or any
 * other target needs to implement.
 */
uint16_t cads_hal_touch_read_raw_x(void) {
    cads_hal_spi_claim_bus();
    cads_hal_spi_set_speed(CadsSpiSpeedTouch);
    uint16_t raw = cads_touch_median(XPT2046_CMD_X_MASKED);
    cads_hal_spi_restore_display_speed();
    cads_hal_spi_release_bus();
    return raw;
}

uint16_t cads_hal_touch_read_raw_y(void) {
    cads_hal_spi_claim_bus();
    cads_hal_spi_set_speed(CadsSpiSpeedTouch);
    uint16_t raw = cads_touch_median(XPT2046_CMD_Y_MASKED);
    cads_hal_spi_restore_display_speed();
    cads_hal_spi_release_bus();
    return raw;
}

/*
 * Diagnostic only: a single unmedianed masked-mode read, byte pair exposed
 * directly instead of pre-combined into the 12 bit result. Added to answer
 * one question precisely - cads_hal_touch_read_raw_x/y() report only 0..15
 * even mid-drag, and (high << 8 | low) >> 3 can only land in that range if
 * high is exactly 0 on every sample. This makes that testable directly
 * instead of inferred from the combined value.
 */
void cads_hal_touch_read_raw_bytes(
    uint8_t* x_high,
    uint8_t* x_low,
    uint8_t* y_high,
    uint8_t* y_low) {
    cads_hal_spi_claim_bus();
    cads_hal_spi_set_speed(CadsSpiSpeedTouch);

    cads_touch_cs(true);
    (void)cads_hal_spi_transfer(XPT2046_CMD_X_MASKED);
    *x_high = cads_hal_spi_transfer(0x00u);
    *x_low = cads_hal_spi_transfer(0x00u);
    cads_touch_cs(false);

    cads_touch_cs(true);
    (void)cads_hal_spi_transfer(XPT2046_CMD_Y_MASKED);
    *y_high = cads_hal_spi_transfer(0x00u);
    *y_low = cads_hal_spi_transfer(0x00u);
    cads_touch_cs(false);

    cads_touch_rearm_penirq();
    cads_hal_spi_restore_display_speed();
    cads_hal_spi_release_bus();
}

bool cads_hal_touch_irq_raw(void) {
    return cads_touch_pressed();
}

void cads_hal_touch_set_calibration(
    uint16_t x_min,
    uint16_t x_max,
    uint16_t y_min,
    uint16_t y_max) {
    cads_touch_calibration.x_min = x_min;
    cads_touch_calibration.x_max = x_max;
    cads_touch_calibration.y_min = y_min;
    cads_touch_calibration.y_max = y_max;
}
