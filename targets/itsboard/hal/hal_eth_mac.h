/*
 * CaDS Zero - the RMII data path: MAC + DMA, frame transmit and receive.
 *
 * Read docs/explanation/pa7-conflict.md and docs/SAFETY.md section 6 before
 * touching this file. RMII is what makes PA7 contended with the display -
 * MDIO (hal_eth_mdio.h) does not need this module at all.
 */

#ifndef CADS_HAL_ETH_MAC_H
#define CADS_HAL_ETH_MAC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Configure the RMII pins, the MAC and the DMA descriptor rings.
 *
 * Call after cads_hal_eth_mdio_init() (shares its RCC/SYSCFG setup - see
 * that function's own comment) and after the PHY has actually resolved a
 * link (cads_hal_eth_phy_status()), since `full_duplex`/`speed_100` come
 * from that result rather than being guessed.
 *
 * Deliberately does not touch PA7's alternate function - hal_spi.c's
 * cads_hal_spi_claim_bus()/release_bus() owns that once
 * cads_hal_spi_set_eth_datapath_active(true) is called (below). Calling
 * this twice is safe; the second call re-applies the same configuration.
 */
void cads_hal_eth_mac_init(const uint8_t mac_address[6], bool full_duplex, bool speed_100);

/**
 * Start the transmitter and receiver (MACCR.TE/RE) and the DMA engines.
 *
 * Call cads_hal_spi_set_eth_datapath_active(true) first - hal_spi.c's blit
 * arbitration needs to know RMII is live before this runs, or the first
 * blit after this call won't know to stop a MAC that just started.
 */
void cads_hal_eth_mac_start(void);

/** Stop the transmitter, receiver and DMA. Safe to call when already
 *  stopped. Call cads_hal_spi_set_eth_datapath_active(false) afterwards. */
void cads_hal_eth_mac_stop(void);

/**
 * Hand one frame to the DMA for transmission.
 *
 * Copies into the next free TX buffer rather than transmitting the
 * caller's buffer directly, so the caller may reuse or free `data`
 * immediately after this returns. Returns false, changing nothing, if
 * `length` is 0 or larger than the buffer size this driver was built with,
 * or if every TX descriptor is still owned by the DMA (the ring is full -
 * the caller is producing faster than the link can drain, or a blit has
 * PA7 right now and TE is stopped).
 */
bool cads_hal_eth_mac_transmit(const uint8_t* data, uint16_t length);

/**
 * Copy the oldest fully-received frame into `buffer`.
 *
 * Returns the frame length on success, or 0 if nothing is waiting. A
 * received frame longer than `buffer_size` is dropped - counted, not
 * truncated silently into the caller's buffer - and this still returns 0
 * for that call; the caller sees "nothing", not a wrong length.
 */
uint16_t cads_hal_eth_mac_receive(uint8_t* buffer, uint16_t buffer_size);

#endif /* CADS_HAL_ETH_MAC_H */
