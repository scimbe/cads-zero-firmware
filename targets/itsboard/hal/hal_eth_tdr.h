#ifndef CADS_HAL_ETH_TDR_H
#define CADS_HAL_ETH_TDR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * TDR (Time Domain Reflectometry) cable diagnostics on the LAN8742A.
 *
 * The PHY transmits a pulse on one twisted pair and times the reflection off
 * whatever impedance discontinuity it finds - an open end, a short, or (on an
 * active 100 Mb link) the far end's termination - and reports both what it
 * found and, for the fault cases, an electrical length that converts to
 * metres with a cable-type-dependent constant. All of this is over MDIO: it
 * never touches PA7, so it needs neither the RMII data path nor the
 * SB121/SB122 decision. See docs/explanation/pa7-conflict.md.
 *
 * Register sequence, propagation constants and error bounds are taken
 * directly from Microchip DS00001989A (LAN8742A/LAN8742Ai datasheet),
 * section 3.8.9 and Figure 3-16, not inferred or guessed.
 *
 * THIS TEST IS DISRUPTIVE. Running it on the open/short path forces the PHY
 * out of auto-negotiation and briefly breaks any active link on the tested
 * pair - the datasheet says so explicitly. cads_hal_eth_tdr_run() saves the
 * PHY's prior BCR/reg27 state and restores it before returning, but the link
 * itself still has to renegotiate afterwards; do not call this while the link
 * is depended on for anything else.
 */

typedef enum {
    CadsEthCableUnknown = 0, /**< mixed or unknown cable in the run - the
                               *   conservative default with the widest
                               *   documented error bound */
    CadsEthCableCat5,
    CadsEthCableCat5e,
    CadsEthCableCat6,
} cads_eth_cable_type_hint_t;

typedef enum {
    CadsEthChannelMdi = 0, /**< TX pair in a stock (non-crossed) run */
    CadsEthChannelMdix = 1, /**< RX pair - the datasheet's flow tests both */
} cads_eth_tdr_channel_t;

typedef enum {
    CadsEthCableDefault = 0, /**< test did not resolve a condition */
    CadsEthCableShorted = 1,
    CadsEthCableOpen = 2,
    CadsEthCableMatched = 3, /**< no discontinuity found - good cable, or the
                               *   far end is simply not there to reflect     */
} cads_eth_cable_condition_t;

typedef struct {
    bool completed;                    /**< false: timed out, nothing else in
                                         *   this struct is meaningful        */
    cads_eth_tdr_channel_t channel;
    cads_eth_cable_condition_t condition;
    uint8_t raw_length;                /**< the PHY's raw electrical length,
                                         *   0..255; meaningless when
                                         *   condition is Matched or Default  */
    uint16_t distance_m;               /**< 0 when raw_length could not be
                                         *   converted (Matched/Default)      */
} cads_eth_tdr_result_t;

/**
 * Run TDR on one channel and restore the PHY's prior state afterwards.
 *
 * Blocks for up to a few hundred milliseconds while the PHY completes the
 * measurement. `cable_hint` selects the propagation constant from the
 * datasheet's Table 3-8; CadsEthCableUnknown is the safe default when the
 * cable type is not known, at the cost of a wider error bound (see
 * docs/reference/measurements.md once this is verified on hardware).
 */
bool cads_hal_eth_tdr_run(
    uint8_t phy,
    cads_eth_tdr_channel_t channel,
    cads_eth_cable_type_hint_t cable_hint,
    cads_eth_tdr_result_t* result);

/**
 * Estimate distance to the link partner on an ALREADY-UP 100 Mb link.
 *
 * Non-disruptive: reads the Cable Length Register and looks it up in the
 * datasheet's Table 3-11. Returns false if there is no active 100 Mb link,
 * in which case there is nothing valid to read - this is not the open/short
 * path and does not force anything.
 */
bool cads_hal_eth_cable_length_matched(uint8_t phy, uint16_t* distance_m);

#endif /* CADS_HAL_ETH_TDR_H */
