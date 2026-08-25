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

#define XPT2046_CMD_X 0xD0u /* differential, 12 bit, X position */
#define XPT2046_CMD_Y 0x90u /* differential, 12 bit, Y position */

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
    cads_touch_cs(true);
    (void)cads_hal_spi_transfer(command);
    cads_hal_delay_us(10u); /* conversion time */
    uint8_t high = cads_hal_spi_transfer(0x00u);
    uint8_t low = cads_hal_spi_transfer(0x00u);
    cads_touch_cs(false);

    /* 12 significant bits, left aligned in the 16 bit response. */
    return (uint16_t)(((uint16_t)high << 8 | low) >> 3) & 0x0FFFu;
}

static uint16_t cads_touch_median(uint8_t command) {
    uint16_t samples[CADS_TOUCH_SAMPLES];
    for(uint32_t i = 0; i < CADS_TOUCH_SAMPLES; i++) {
        samples[i] = cads_touch_read_axis(command);
    }
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

    uint16_t raw_x = cads_touch_median(XPT2046_CMD_X);
    uint16_t raw_y = cads_touch_median(XPT2046_CMD_Y);

    cads_hal_spi_restore_display_speed();
    cads_hal_spi_release_bus();

    /* Still pressed after sampling? Otherwise the finger left mid-read and the
     * coordinates are garbage. */
    if(!cads_touch_pressed()) return;
    if(raw_x == 0u || raw_x >= 0x0FFFu) return;

    /* The panel is mounted rotated relative to the controller's axes: the
     * controller's Y runs along the display's X. */
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
    uint16_t raw = cads_touch_median(XPT2046_CMD_X);
    cads_hal_spi_restore_display_speed();
    cads_hal_spi_release_bus();
    return raw;
}

uint16_t cads_hal_touch_read_raw_y(void) {
    cads_hal_spi_claim_bus();
    cads_hal_spi_set_speed(CadsSpiSpeedTouch);
    uint16_t raw = cads_touch_median(XPT2046_CMD_Y);
    cads_hal_spi_restore_display_speed();
    cads_hal_spi_release_bus();
    return raw;
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
