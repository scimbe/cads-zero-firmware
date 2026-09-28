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

/**
 * Set or clear promiscuous mode (MACFFR.PM): with it set, the MAC hands
 * every frame it hears to the DMA regardless of destination address,
 * instead of filtering to only frames addressed to this device (unicast/
 * broadcast/joined-multicast). Takes effect immediately; safe to call
 * with the receiver already running.
 */
void cads_hal_eth_mac_set_promiscuous(bool enable);

/**
 * Frames the DMA discarded before software ever saw them - the real,
 * hardware-counted answer to "how much load is this driver actually
 * losing", read from ETH->DMAMFBOCR (RM0090).
 *
 * `no_descriptor`, when not NULL, receives DMAMFBOCR.MFC: frames dropped
 * because every RX descriptor was still owned by software - this is the
 * number that answers "is software (a promiscuous capture, most of all)
 * keeping up". `fifo_overflow`, when not NULL, receives DMAMFBOCR.MFA:
 * frames dropped to Rx FIFO overflow or a runt frame - a MAC/wire-level
 * condition, not a software one.
 *
 * Deliberately not named after DMAMFBOCR's own MFC/MFA field names in
 * this API: RM0090's prose describes MFC (whose field name literally
 * reads "missed frames by the controller") as counting host-receive-
 * buffer-unavailable drops, and MFA ("missed frames by the application")
 * as counting Rx FIFO overflow/runt frames - the field names are each
 * describing the OTHER field's naming intuition. Confirmed by reading
 * RM0090's per-bit descriptions directly rather than trusting the more
 * ambiguous field names (or the CMSIS header's own field comments, which
 * repeat the same names without the clarifying detail) - passing this
 * mismatch on as `no_descriptor`/`fifo_overflow` here means no caller has
 * to rediscover it.
 *
 * Reading ETH->DMAMFBOCR clears both counters as a hardware side effect
 * (RM0090: both fields are `rc_r`, read-clears) - like
 * cads_hal_eth_mmc_read()'s counters, a caller that wants a total across
 * more than one read must accumulate the deltas itself.
 */
void cads_hal_eth_mac_missed_frames(uint32_t* no_descriptor, uint32_t* fifo_overflow);

/** The same two counters as running totals since boot, independent of
 *  cads_hal_eth_mac_missed_frames()'s since-last-call view (both share one
 *  clear-on-read register without stealing each other's counts). */
void cads_hal_eth_mac_missed_totals(uint32_t* no_descriptor, uint32_t* fifo_overflow);

#endif /* CADS_HAL_ETH_MAC_H */
