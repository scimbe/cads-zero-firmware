/*
 * CaDS Zero - ILI9486 panel driver for the Waveshare 4" TFT Touch Shield.
 *
 * BUS SHAPE
 * ---------
 * The shield does not wire SPI to the panel. MOSI feeds a 74HC4040 counter and
 * two cascaded 74HC4094 shift registers which present a 16-bit parallel word
 * to the ILI9486. Two consequences drive every design choice here:
 *
 *   1. The bus is write-only. There is no register readback, no ID check, and
 *      no read-modify-write on video memory. Everything is open loop, so the
 *      framebuffer in RAM is the single source of truth about what is on
 *      screen.
 *   2. A command is one byte with DC low; a parameter or pixel is two bytes
 *      with DC high. This asymmetry is a property of the glue logic, not of
 *      the ILI9486, and it is reproduced exactly from the sequence that has
 *      been driving this shield in the ITS lab for years.
 *
 * The register table below is the panel vendor's power/gamma/timing setup for
 * this specific module. It is hardware configuration data, not something to
 * be re-derived: a wrong VGH or VCOM value is one of the few ways software can
 * actually damage a TFT.
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"
#include "hal_spi.h"

/* ILI9486 commands used here. */
#define ILI9486_SLPOUT  0x11u
#define ILI9486_DISPON  0x29u
#define ILI9486_CASET   0x2Au
#define ILI9486_PASET   0x2Bu
#define ILI9486_RAMWR   0x2Cu
#define ILI9486_MADCTL  0x36u
#define ILI9486_DISCTRL 0xB6u

/*
 * Landscape, 480x320, left-to-right / top-to-bottom scan.
 *
 * MADCTL bits: MY 0x80, MX 0x40, MV 0x20, ML 0x10, BGR 0x08.
 *
 * MV exchanges rows and columns to get landscape. Because of that exchange,
 * MX and MY swap their apparent effect: with MV set it is MY that controls the
 * horizontal scan direction, not MX. Setting MX instead flips the picture
 * vertically, which is how this was pinned down - one bit at a time, with a
 * camera pointed at the panel.
 *
 * MY is therefore needed on top of MV, otherwise the panel scans columns in
 * the opposite order and everything comes out mirrored horizontally.
 *
 * That mirror is invisible in a colour-bar test pattern, which is symmetric in
 * X, and it stayed hidden until the first text was rendered and photographed -
 * the wordmark read backwards. The vendor's own driver compensates for the
 * same effect in software with a coordinate transform on every window write;
 * doing it in MADCTL instead costs nothing per pixel.
 */
#define CADS_MADCTL_LANDSCAPE 0xA8u  /* MY | MV | BGR */
#define CADS_DISCTRL_SCAN     0x22u

/* Power, gamma and frame timing for this module. Format: command, argument
 * count, arguments. */
static const uint8_t cads_ili9486_init[] = {
    0xF9, 2,  0x00, 0x08,
    0xC0, 2,  0x19, 0x1A,       /* VREG1OUT positive, VREG2OUT negative      */
    0xC1, 2,  0x45, 0x00,       /* VGH / VGL                                 */
    0xC2, 1,  0x33,             /* normal mode power                         */
    0xC5, 2,  0x00, 0x28,       /* VCOM, must stay <= 0x80                   */
    0xB1, 2,  0xA0, 0x11,       /* frame rate, 0xA0 = 62 Hz                  */
    0xB4, 1,  0x02,             /* 2 dot frame mode                          */
    0xB6, 3,  0x00, 0x42, 0x3B, /* display function control                  */
    0xB7, 1,  0x07,
    0xE0, 15, 0x1F, 0x25, 0x22, 0x0B, 0x06, 0x0A, 0x4E, 0xC6,
              0x39, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       /* positive gamma */
    0xE1, 15, 0x1F, 0x3F, 0x3F, 0x0F, 0x1F, 0x0F, 0x46, 0x49,
              0x31, 0x05, 0x09, 0x03, 0x1C, 0x1A, 0x00,       /* negative gamma */
    0xF1, 8,  0x36, 0x04, 0x00, 0x3C, 0x0F, 0x0F, 0xA4, 0x02,
    0xF2, 9,  0x18, 0xA3, 0x12, 0x02, 0x32, 0x12, 0xFF, 0x32, 0x00,
    0xF4, 5,  0x40, 0x00, 0x08, 0x91, 0x04,
    0xF8, 2,  0x21, 0x04,
    0x3A, 1,  0x55, /* interface pixel format: 16 bit / RGB565 */
};

static inline void cads_lcd_cs(bool active) {
    cads_gpio_write(CADS_PIN_LCD_CS_PORT, CADS_PIN_LCD_CS, !active);
}

static inline void cads_lcd_dc_command(void) {
    cads_gpio_write(CADS_PIN_LCD_DC_PORT, CADS_PIN_LCD_DC, false);
}

static inline void cads_lcd_dc_data(void) {
    cads_gpio_write(CADS_PIN_LCD_DC_PORT, CADS_PIN_LCD_DC, true);
}

static void cads_lcd_command(uint8_t command) {
    cads_lcd_dc_command();
    cads_lcd_cs(true);
    (void)cads_hal_spi_transfer(command);
    cads_lcd_cs(false);
}

/** One parameter, sent as a 16 bit word: high byte then low byte. */
static void cads_lcd_param(uint16_t value) {
    cads_lcd_dc_data();
    cads_lcd_cs(true);
    (void)cads_hal_spi_transfer((uint8_t)(value >> 8));
    (void)cads_hal_spi_transfer((uint8_t)(value & 0xFFu));
    cads_lcd_cs(false);
}

/* --- backlight: PD15 = TIM4_CH4, AF2 -------------------------------------- */

static void cads_lcd_backlight_init(void) {
    cads_gpio_init_alternate(CADS_PIN_LCD_BL_PORT, CADS_PIN_LCD_BL, 2u, CadsGpioPullNone);

    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    (void)RCC->APB1ENR;

    /* APB1 is /4 so the timer clock is 2 x PCLK1 = 90 MHz.
     * 90 MHz / 90 / 1000 = 1 kHz, with 0..1000 as the duty range. */
    TIM4->PSC = (CADS_PCLK1_HZ * 2u) / 1000000u - 1u;
    TIM4->ARR = 1000u - 1u;
    TIM4->CCR4 = 0u;
    TIM4->CCMR2 = (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE; /* PWM mode 1 */
    TIM4->CCER = TIM_CCER_CC4E;
    TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    TIM4->EGR = TIM_EGR_UG;
}

void cads_hal_display_backlight(uint8_t percent) {
    if(percent > 100u) percent = 100u;
    TIM4->CCR4 = (uint32_t)percent * 10u;
}

/* --- init ----------------------------------------------------------------- */

void cads_hal_display_init(void) {
    cads_gpio_init_output(CADS_PIN_LCD_CS_PORT, CADS_PIN_LCD_CS, true);   /* idle high */
    cads_gpio_init_output(CADS_PIN_LCD_DC_PORT, CADS_PIN_LCD_DC, true);
    cads_gpio_init_output(CADS_PIN_LCD_RST_PORT, CADS_PIN_LCD_RST, true);

    cads_lcd_backlight_init();

    cads_hal_spi_claim_bus();
    cads_hal_spi_set_speed(CadsSpiSpeedDisplay);

    /* Hardware reset. The datasheet asks for 10 ms; the module's RC network
     * wants considerably more, and the vendor sequence uses 500 ms per edge.
     * Boot time is not worth a flaky panel. */
    cads_gpio_write(CADS_PIN_LCD_RST_PORT, CADS_PIN_LCD_RST, true);
    cads_hal_delay_ms(120u);
    cads_gpio_write(CADS_PIN_LCD_RST_PORT, CADS_PIN_LCD_RST, false);
    cads_hal_delay_ms(120u);
    cads_gpio_write(CADS_PIN_LCD_RST_PORT, CADS_PIN_LCD_RST, true);
    cads_hal_delay_ms(120u);

    for(size_t i = 0; i < sizeof(cads_ili9486_init);) {
        uint8_t command = cads_ili9486_init[i++];
        uint8_t argument_count = cads_ili9486_init[i++];
        cads_lcd_command(command);
        while(argument_count--) {
            cads_lcd_param(cads_ili9486_init[i++]);
        }
    }

    cads_lcd_command(ILI9486_DISCTRL);
    cads_lcd_param(0x00u);
    cads_lcd_param(CADS_DISCTRL_SCAN);

    cads_lcd_command(ILI9486_MADCTL);
    cads_lcd_param(CADS_MADCTL_LANDSCAPE);

    cads_hal_delay_ms(200u);
    cads_lcd_command(ILI9486_SLPOUT);
    cads_hal_delay_ms(120u);
    cads_lcd_command(ILI9486_DISPON);

    cads_hal_spi_release_bus();
}

/* --- blitting -------------------------------------------------------------- */

static void cads_lcd_set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    uint16_t x_end = (uint16_t)(x + width - 1u);
    uint16_t y_end = (uint16_t)(y + height - 1u);

    cads_lcd_command(ILI9486_CASET);
    cads_lcd_param((uint16_t)(x >> 8));
    cads_lcd_param((uint16_t)(x & 0xFFu));
    cads_lcd_param((uint16_t)(x_end >> 8));
    cads_lcd_param((uint16_t)(x_end & 0xFFu));

    cads_lcd_command(ILI9486_PASET);
    cads_lcd_param((uint16_t)(y >> 8));
    cads_lcd_param((uint16_t)(y & 0xFFu));
    cads_lcd_param((uint16_t)(y_end >> 8));
    cads_lcd_param((uint16_t)(y_end & 0xFFu));
}

/*
 * Pixels go out as big-endian RGB565. The framebuffer stores native little
 * endian halfwords, so the byte order is fixed up by the flush path in
 * gui/canvas.c before the buffer reaches DMA - doing it here would mean
 * touching the caller's memory.
 */
void cads_hal_display_blit(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t* pixels) {
    if(width == 0u || height == 0u) return;
    if(x >= CADS_DISPLAY_WIDTH || y >= CADS_DISPLAY_HEIGHT) return;
    if((uint32_t)x + width > CADS_DISPLAY_WIDTH) return;
    if((uint32_t)y + height > CADS_DISPLAY_HEIGHT) return;

    cads_hal_spi_claim_bus();

    cads_lcd_set_window(x, y, width, height);
    cads_lcd_command(ILI9486_RAMWR);

    cads_lcd_dc_data();
    cads_lcd_cs(true);
    cads_hal_spi_write_dma(pixels, (size_t)width * height * 2u);
    cads_hal_spi_wait();
    cads_lcd_cs(false);

    cads_hal_spi_release_bus();
}

void cads_hal_display_set_fast_clock(bool fast) {
    cads_hal_spi_wait();
    cads_hal_spi_set_speed(fast ? CadsSpiSpeedDisplayFast : CadsSpiSpeedDisplay);
}

bool cads_hal_display_busy(void) {
    return cads_hal_spi_busy();
}

void cads_hal_display_wait(void) {
    cads_hal_spi_wait();
}
