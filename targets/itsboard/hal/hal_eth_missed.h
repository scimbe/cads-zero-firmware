/*
 * CaDS Zero - decoding ETH_DMAMFBOCR (RM0090 33.8.1, "missed frame and buffer
 * overflow counter"). Pure, so the host tests it (tests/unit/test_eth_missed.c);
 * hal_eth_mac.c reads the register and feeds the value in.
 *
 *   bits 15:0   MFC   frames missed: no free receive descriptor (ring full)
 *   bit  16     OMFC  MFC overflowed since the last read
 *   bits 27:17  MFA   frames missed by the application: RX FIFO overflow
 *   bit  28     OFOC  MFA overflowed since the last read
 *
 * The register clears on read, so one read = the counts since the previous
 * one. An overflow bit means the counter wrapped at least once: the decoded
 * value then adds one full counter range (a lower bound, never an
 * undercount that looks like "fine").
 */

#ifndef CADS_HAL_ETH_MISSED_H
#define CADS_HAL_ETH_MISSED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t no_descriptor; /**< MFC (+ 0x10000 if OMFC) */
    uint32_t fifo_overflow; /**< MFA (+ 0x800 if OFOC) */
} cads_eth_missed_t;

cads_eth_missed_t cads_eth_missed_decode(uint32_t dmamfbocr);

#ifdef __cplusplus
}
#endif

#endif /* CADS_HAL_ETH_MISSED_H */
