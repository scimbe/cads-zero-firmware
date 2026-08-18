/*
 * CaDS Zero - TDR cable diagnostics for the LAN8742A.
 *
 * Register bits, the write sequence and the propagation constants are taken
 * from Microchip DS00001989A section 3.8.9 / Figure 3-16, transcribed
 * exactly rather than approximated - a wrong propagation constant would not
 * crash anything, it would just quietly report the wrong distance to a
 * cable fault, which defeats the entire point of the feature.
 */

#include "hal_eth_tdr.h"

#include "cads_hal.h"
#include "hal_eth_mdio.h"

/* Basic Control Register (0), the same register hal_eth_mdio.c reads for
 * identity - reused here, not redefined, so the two files agree on layout. */
#define PHY_BCR             0x00u
#define PHY_BCR_DUPLEX_FULL (1u << 8)
#define PHY_BCR_ANEG_ENABLE (1u << 12)
#define PHY_BCR_SPEED_100   (1u << 13)

/* Register 24 (TDR Patterns/Delay Control) is left at its reset default -
 * Figure 3-16's verified sequence never writes it, so neither do we. */

#define PHY_TDR_CTRL              25u /* TDR Control/Status Register       */
#define PHY_TDR_CTRL_ENABLE       (1u << 15)
#define PHY_TDR_CTRL_FILTER_EN    (1u << 14)
#define PHY_TDR_CTRL_TYPE_Pos     9u
#define PHY_TDR_CTRL_TYPE_Msk     (0x3u << PHY_TDR_CTRL_TYPE_Pos)
#define PHY_TDR_CTRL_COMPLETE     (1u << 8)
#define PHY_TDR_CTRL_LENGTH_Msk   0x00FFu

#define PHY_SPECIAL_CTRL_STATUS      27u /* Special Control/Status Indications */
#define PHY_SCSI_AMDIXCTRL_DISABLE   (1u << 15)
#define PHY_SCSI_CH_SELECT_MDIX      (1u << 13)

#define PHY_CABLE_LENGTH      28u /* Cable Length Register */
#define PHY_CABLE_LENGTH_Pos  12u
#define PHY_CABLE_LENGTH_Msk  (0xFu << PHY_CABLE_LENGTH_Pos)

#define PHY_BSR         0x01u
#define PHY_BSR_LINK_UP (1u << 2)

/* Table 3-8: TDR propagation constants, x1000 fixed point (avoids pulling in
 * float promotion rules for a handful of constant multiplies; the datasheet
 * itself only carries three significant figures, so the precision loss here
 * is nothing next to the +/-(few-to-20)m measurement error the datasheet
 * documents for the underlying TDR reading). */
typedef struct {
    uint16_t p_open_x1000;
    uint16_t p_short_x1000;
} cads_tdr_constant_t;

static const cads_tdr_constant_t cads_tdr_constants[] = {
    [CadsEthCableUnknown] = {769u, 793u},
    [CadsEthCableCat5] = {850u, 873u},
    [CadsEthCableCat5e] = {760u, 788u},
    [CadsEthCableCat6] = {745u, 759u},
};

/* Table 3-11: matched-cable CBLN lookup, index 0..15, metres. Indices 0-3 all
 * mean "no measurable distance" per the datasheet and share the entry. */
static const uint16_t cads_matched_length_lookup[16] = {
    0, 0, 0, 0, 6, 17, 27, 38, 49, 59, 70, 81, 91, 102, 113, 123};

/* Generous but bounded: the datasheet gives no maximum, and a stuck TDR_
 * ENABLE bit must not hang the caller forever on a bench with nobody
 * watching. */
#define CADS_TDR_POLL_ATTEMPTS 2000u
#define CADS_TDR_POLL_DELAY_US 500u /* -> up to ~1 s total */

bool cads_hal_eth_tdr_run(
    uint8_t phy,
    cads_eth_tdr_channel_t channel,
    cads_eth_cable_type_hint_t cable_hint,
    cads_eth_tdr_result_t* result) {
    result->completed = false;
    result->channel = channel;

    uint16_t saved_bcr, saved_scsi;
    if(!cads_hal_eth_mdio_read(phy, PHY_BCR, &saved_bcr)) return false;
    if(!cads_hal_eth_mdio_read(phy, PHY_SPECIAL_CTRL_STATUS, &saved_scsi)) return false;

    /* Figure 3-16, step 1: force 100 Mb full duplex, auto-negotiation off.
     * TDR's timing measurement is only characterised in this mode. */
    uint16_t forced_bcr = (uint16_t)((saved_bcr & ~(uint16_t)PHY_BCR_ANEG_ENABLE) |
                                     PHY_BCR_SPEED_100 | PHY_BCR_DUPLEX_FULL);
    if(!cads_hal_eth_mdio_write(phy, PHY_BCR, forced_bcr)) goto restore_and_fail;

    /* Step 2: disable HP Auto-MDIX and select which pair TDR drives - TX in
     * MDI mode, RX in MDIX mode. Testing both is the caller's job (call this
     * function twice); one call tests exactly one pair. */
    uint16_t channel_bits = PHY_SCSI_AMDIXCTRL_DISABLE;
    if(channel == CadsEthChannelMdix) channel_bits |= PHY_SCSI_CH_SELECT_MDIX;
    if(!cads_hal_eth_mdio_write(phy, PHY_SPECIAL_CTRL_STATUS, channel_bits)) {
        goto restore_and_fail;
    }

    /* Step 3: start TDR. The analog-to-digital filter bit exists to reduce
     * noise spikes but is not part of the datasheet's verified sequence
     * (Figure 3-16 writes exactly 0x8000), so it is left off here too. */
    if(!cads_hal_eth_mdio_write(phy, PHY_TDR_CTRL, PHY_TDR_CTRL_ENABLE)) {
        goto restore_and_fail;
    }

    uint16_t status = 0u;
    bool completed = false;
    for(uint32_t attempt = 0; attempt < CADS_TDR_POLL_ATTEMPTS; attempt++) {
        if(!cads_hal_eth_mdio_read(phy, PHY_TDR_CTRL, &status)) goto restore_and_fail;
        if(status & PHY_TDR_CTRL_COMPLETE) {
            completed = true;
            break;
        }
        cads_hal_delay_us(CADS_TDR_POLL_DELAY_US);
    }

    if(completed) {
        result->condition =
            (cads_eth_cable_condition_t)((status & PHY_TDR_CTRL_TYPE_Msk) >> PHY_TDR_CTRL_TYPE_Pos);
        result->raw_length = (uint8_t)(status & PHY_TDR_CTRL_LENGTH_Msk);

        const cads_tdr_constant_t* k = &cads_tdr_constants[cable_hint];
        if(result->condition == CadsEthCableOpen) {
            result->distance_m = (uint16_t)(((uint32_t)result->raw_length * k->p_open_x1000) / 1000u);
        } else if(result->condition == CadsEthCableShorted) {
            result->distance_m = (uint16_t)(((uint32_t)result->raw_length * k->p_short_x1000) / 1000u);
        } else {
            /* Matched or Default: the datasheet says the length field is not
             * valid here. Reporting 0 rather than a number computed from a
             * meaningless field is the honest choice. */
            result->distance_m = 0u;
        }
        result->completed = true;
    }

restore_and_fail:
    /* Always restore, whether TDR completed, timed out, or an MDIO write
     * failed partway through - leaving the PHY forced out of auto-negotiate
     * because one register write failed would be a worse outcome than the
     * failed measurement itself. */
    (void)cads_hal_eth_mdio_write(phy, PHY_SPECIAL_CTRL_STATUS, saved_scsi);
    (void)cads_hal_eth_mdio_write(phy, PHY_BCR, saved_bcr);
    return result->completed;
}

bool cads_hal_eth_cable_length_matched(uint8_t phy, uint16_t* distance_m) {
    uint16_t bsr;
    if(!cads_hal_eth_mdio_read(phy, PHY_BSR, &bsr)) return false;
    if(!(bsr & PHY_BSR_LINK_UP)) return false; /* nothing valid to read */

    uint16_t cable_length_reg;
    if(!cads_hal_eth_mdio_read(phy, PHY_CABLE_LENGTH, &cable_length_reg)) return false;

    uint32_t cbln = (cable_length_reg & PHY_CABLE_LENGTH_Msk) >> PHY_CABLE_LENGTH_Pos;
    *distance_m = cads_matched_length_lookup[cbln];
    return true;
}
