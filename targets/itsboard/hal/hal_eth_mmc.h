#ifndef CADS_HAL_ETH_MMC_H
#define CADS_HAL_ETH_MMC_H

#include <stdint.h>

/**
 * MAC Management Counters (MMC): free-running Ethernet traffic statistics
 * built into the STM32F429's MAC itself.
 *
 * Unlike hal_eth_mdio.c/hal_eth_aneg.c/hal_eth_linklog.c, this is not a PHY
 * feature reached over MDIO - the MMC block lives inside the MAC and is read
 * as ordinary memory-mapped registers (ETH->MMC*, RM0090 section 34.7). No
 * PHY address, no bus transaction, nothing that can time out.
 *
 * These six counters (Registers `MMCTGFCR`, `MMCTGFSCCR`, `MMCTGFMSCCR`,
 * `MMCRGUFCR`, `MMCRFCECR`, `MMCRFAECR`) are not a curated subset chosen for
 * scope discipline - they are the ENTIRE counter set the STM32F429's MMC
 * block implements. RM0090's MMC register map has no broadcast, multicast,
 * oversize or undersize frame counters; the CMSIS `ETH_TypeDef` struct
 * (lib/cmsis_device_f4/Include/stm32f429xx.h) has no fields for them either,
 * confirmed against the reserved-word gaps between the registers that do
 * exist. This is a smaller counter block than the full IEEE 802.3 clause 30
 * recommendation, and there is no way to read what silicon does not have.
 *
 * Counting starts as soon as the MAC clock is running and frames flow -
 * nothing needs to be armed. Every counter is 32 bits and wraps to zero on
 * overflow by hardware default (MMCCR.CSR, Counter Stop Rollover, reset
 * value 0); this module does not touch that bit, so a link busy enough to
 * wrap a counter between two polls will do so silently. A caller that cares
 * about totals across a long session must diff successive reads itself.
 */

typedef struct {
    uint32_t tx_good_frames;                 /**< MMCTGFCR: good frames transmitted */
    uint32_t tx_good_after_single_collision; /**< MMCTGFSCCR: half-duplex only */
    uint32_t tx_good_after_multi_collision;  /**< MMCTGFMSCCR: half-duplex only */
    uint32_t rx_good_unicast_frames;         /**< MMCRGUFCR: good unicast frames received */
    uint32_t rx_crc_errors;                  /**< MMCRFCECR: frames received with a CRC error */
    uint32_t rx_alignment_errors;            /**< MMCRFAECR: frames received with an alignment
                                               *   (dribble) error */
} cads_eth_mmc_counters_t;

/**
 * Read all six counters. Cannot fail - these are ordinary loads from a
 * peripheral that is either mapped or the caller has bigger problems, unlike
 * an MDIO transaction, which is why this returns void rather than following
 * hal_eth_mdio.c's early-return-false idiom.
 *
 * Precondition: the MAC clock must already be enabled
 * (RCC_AHB1ENR_ETHMACEN), which cads_hal_eth_mdio_init() does as a side
 * effect of bringing up management access. This module does not enable it
 * itself and does not depend on hal_eth_mdio.h - call mdio_init() (or
 * otherwise enable the clock) first, same precondition hal_eth_mdio.c places
 * on the GPIO/SYSCFG setup it assumes was not done by someone else already.
 */
void cads_hal_eth_mmc_read(cads_eth_mmc_counters_t* counters);

/**
 * Zero all six counters at once (MMCCR.CR, Counters Reset - self-clearing,
 * hardware resets the bit once the clear completes). There is no per-counter
 * reset; this is all six or none.
 *
 * Deliberately the only way this module clears anything: MMCCR also has a
 * Reset-On-Read bit (ROR) that would zero a counter the instant
 * cads_hal_eth_mmc_read() looked at it, but this module never sets it. ROR
 * is a MAC-wide setting, not a per-caller one - turning it on would silently
 * zero counters for any other code that reads the same registers, which is
 * a surprise this module has no business creating. A read here is always
 * non-destructive; call this function when a caller actually wants a reset.
 */
void cads_hal_eth_mmc_reset(void);

#endif /* CADS_HAL_ETH_MMC_H */
