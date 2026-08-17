#ifndef CADS_HAL_SPI_H
#define CADS_HAL_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CadsSpiSpeedDisplay,     /**< PCLK2/16 = 5.6 MHz, the always-safe divider */
    CadsSpiSpeedDisplayFast, /**< PCLK2/8  = 11.3 MHz, qualified on hardware  */
    CadsSpiSpeedTouch,       /**< PCLK2/128, the XPT2046 needs a slow clock   */
} cads_spi_speed_t;

void cads_hal_spi_init(void);
void cads_hal_spi_set_speed(cads_spi_speed_t speed);

/** Return the bus to whichever display divider is currently selected, after a
 *  driver has temporarily dropped it (the touch controller does). */
void cads_hal_spi_restore_display_speed(void);

/**
 * Take ownership of the SPI1 MOSI pin.
 *
 * On a stock board this stops the Ethernet MAC and steals PA7 from the PHY;
 * calls nest, and the MAC only restarts when the outermost claim is released.
 * After the SB121/SB122 swap both become no-ops.
 *
 * Every display or touch access must sit between claim and release.
 */
void cads_hal_spi_claim_bus(void);
void cads_hal_spi_release_bus(void);

uint8_t cads_hal_spi_transfer(uint8_t value);
void cads_hal_spi_write(const uint8_t* data, size_t length);

/** Start a DMA transmit. Returns immediately; the buffer must stay valid and
 *  must live in DMA-capable SRAM (never CCM). */
void cads_hal_spi_write_dma(const void* data, size_t length);

bool cads_hal_spi_busy(void);
void cads_hal_spi_wait(void);

#endif /* CADS_HAL_SPI_H */
