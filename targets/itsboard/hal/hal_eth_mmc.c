/*
 * CaDS Zero - MAC Management Counters (MMC) for the STM32F429's Ethernet MAC.
 *
 * Register offsets come from the CMSIS device header
 * (lib/cmsis_device_f4/Include/stm32f429xx.h), specifically the `ETH_TypeDef`
 * struct's MMC members and the `ETH_MMCCR_*`/`ETH_MMC*CR_*` bit
 * definitions - transcribed from there, not from memory, matching this
 * project's rule for register values. The reserved-word gaps in that struct
 * between MMCTIMR and MMCTGFSCCR (14 words), MMCTGFMSCCR and MMCTGFCR (5
 * words), MMCTGFCR and MMCRFCECR (10 words), and MMCRFAECR and MMCRGUFCR (10
 * words) place each counter at the RM0090-documented offset (0x14C, 0x150,
 * 0x168, 0x194, 0x198, 0x1C4 from ETH_BASE) without this file needing to
 * name a single address itself.
 */

#include "hal_eth_mmc.h"

#include "board.h"

void cads_hal_eth_mmc_read(cads_eth_mmc_counters_t* counters) {
    counters->tx_good_frames = ETH->MMCTGFCR;
    counters->tx_good_after_single_collision = ETH->MMCTGFSCCR;
    counters->tx_good_after_multi_collision = ETH->MMCTGFMSCCR;
    counters->rx_good_unicast_frames = ETH->MMCRGUFCR;
    counters->rx_crc_errors = ETH->MMCRFCECR;
    counters->rx_alignment_errors = ETH->MMCRFAECR;
}

void cads_hal_eth_mmc_reset(void) {
    ETH->MMCCR |= ETH_MMCCR_CR; /* self-clearing: hardware drops CR once the
                                  * reset completes, no poll-for-done needed. */
}
