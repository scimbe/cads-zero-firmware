#ifndef CADS_HAL_ETH_ANEG_H
#define CADS_HAL_ETH_ANEG_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Auto-negotiation advertisement inspector for the LAN8742A.
 *
 * Decodes what the local PHY advertised (Register 4, ANAR) and what the link
 * partner advertised back (Register 5, ANLPAR) into the individual
 * capabilities IEEE 802.3 clause 28 defines, plus the highest-common-
 * denominator mode both sides actually support - the question a raw register
 * dump does not answer directly, and the one that actually explains a
 * speed/duplex mismatch. Bit positions are IEEE 802.3 clause 22/28 standard
 * registers, the same layout on essentially every 10/100 PHY, not the
 * vendor-specific registers hal_eth_tdr.c reads.
 *
 * Non-disruptive: two MDIO reads, no writes, no effect on an active link or
 * an in-progress negotiation. Unlike cads_hal_eth_tdr_run(), safe to call at
 * any time.
 */

typedef struct {
    bool half_10;
    bool full_10;
    bool half_100;
    bool full_100;
    bool pause;        /**< symmetric PAUSE, IEEE 802.3 Annex 28B.3 */
    bool pause_asym;    /**< asymmetric PAUSE */
    bool remote_fault; /**< set by the advertiser to signal a fault on its own side */
} cads_eth_aneg_ability_t;

typedef enum {
    CadsEthAnegModeNone = 0, /**< no capability in common - the link should not be up */
    CadsEthAnegMode10Half,
    CadsEthAnegMode10Full,
    CadsEthAnegMode100Half,
    CadsEthAnegMode100Full,
} cads_eth_aneg_mode_t;

typedef struct {
    bool completed;                  /**< false: an MDIO read failed, nothing
                                       *   else in this struct is meaningful */
    uint16_t anar_raw;
    uint16_t anlpar_raw;
    cads_eth_aneg_ability_t local;   /**< decoded from ANAR: what this PHY offers */
    cads_eth_aneg_ability_t partner; /**< decoded from ANLPAR: what the link
                                       *   partner offered back */
    bool partner_acknowledged;       /**< ANLPAR bit 14: the partner received
                                       *   this PHY's base link code word */
    cads_eth_aneg_mode_t resolved;   /**< highest-common-denominator mode by
                                       *   IEEE 802.3 Table 28B-3 priority
                                       *   (100 full > 100 half > 10 full >
                                       *   10 half); CadsEthAnegModeNone if
                                       *   local and partner share nothing */
} cads_eth_aneg_report_t;

/**
 * Read and decode Registers 4 and 5. Safe to call at any time, including
 * while negotiation is still in progress - `partner` and `resolved` then
 * simply reflect whatever ANLPAR held at the moment of the read.
 */
bool cads_hal_eth_aneg_report(uint8_t phy, cads_eth_aneg_report_t* report);

#endif /* CADS_HAL_ETH_ANEG_H */
