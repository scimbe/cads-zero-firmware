#ifndef CADS_HAL_ETH_MDIO_H
#define CADS_HAL_ETH_MDIO_H

#include <stdbool.h>
#include <stdint.h>

/**
 * PHY management over MDIO.
 *
 * Deliberately independent of the RMII data path: management needs only PA2
 * and PC1, so the PHY can be identified and its link read WITHOUT touching
 * PA7, which the display owns. See docs/explanation/pa7-conflict.md.
 */

typedef struct {
    uint16_t id1, id2;
    uint32_t oui;      /**< 22-bit organisationally unique identifier */
    uint8_t model;
    uint8_t revision;
    uint16_t bsr;
    bool link_up;
    bool autoneg_done;
    uint16_t speed_mbit; /**< 0 when not resolved */
    bool full_duplex;
} cads_eth_phy_status_t;

void cads_hal_eth_mdio_init(void);
bool cads_hal_eth_mdio_read(uint8_t phy, uint8_t reg, uint16_t* value);
bool cads_hal_eth_mdio_write(uint8_t phy, uint8_t reg, uint16_t value);

/** Returns false when no PHY answers at `phy`, which is different from a PHY
 *  that answers and reports the link down. */
bool cads_hal_eth_phy_status(uint8_t phy, cads_eth_phy_status_t* status);

#endif /* CADS_HAL_ETH_MDIO_H */
