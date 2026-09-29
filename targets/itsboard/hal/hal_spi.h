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
 * Declare whether the RMII data path is up and therefore owns PA7.
 *
 * The Ethernet driver sets this when it brings RMII up and clears it when it
 * takes it down. Until then the display keeps the pin and the per-blit
 * arbitration is skipped entirely - which matters because reading the PHY over
 * MDIO enables the ETH clock without touching PA7 at all, and arbitrating
 * against a data path that does not exist would cost every blit a pointless
 * MAC stop and restart.
 */
void cads_hal_spi_set_eth_datapath_active(bool active);

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

/** Start an 8-bit DMA transmit. Returns immediately; the buffer must stay valid
 *  and must live in DMA-capable SRAM (never CCM). */
void cads_hal_spi_write_dma(const void* data, size_t length);

bool cads_hal_spi_busy(void);
void cads_hal_spi_wait(void);

#endif /* CADS_HAL_SPI_H */
