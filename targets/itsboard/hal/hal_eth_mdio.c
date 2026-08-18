/*
 * CaDS Zero - LAN8742A management interface (MDIO).
 *
 * WHY THIS IS SEPARATE FROM THE DATA PATH
 * ---------------------------------------
 * The PHY is reached over two independent interfaces. RMII carries frames and
 * needs PA7 for CRS_DV - the pin the display also wants, see
 * docs/explanation/pa7-conflict.md. MDIO carries management registers and
 * needs only PA2 (MDIO) and PC1 (MDC).
 *
 * So link state, PHY identity, speed and duplex can all be read WITHOUT
 * touching the contested pin and without disturbing the display at all. That
 * makes it the one part of the Ethernet subsystem that can be brought up and
 * tested on a stock board with no compromise, which is why it comes first.
 *
 * MDIO is a slow, half-duplex serial bus clocked by MDC. The MAC generates the
 * frame; software only sets up the address and polls MB.
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_eth_mdio.h"
#include "hal_gpio.h"

/* IEEE 802.3 clause 22 standard registers. */
#define PHY_BCR      0x00u /* basic control                                  */
#define PHY_BSR      0x01u /* basic status                                   */
#define PHY_ID1      0x02u /* OUI high                                       */
#define PHY_ID2      0x03u /* OUI low, model, revision                       */
#define PHY_ANAR     0x04u /* auto-negotiation advertisement                 */
#define PHY_ANLPAR   0x05u /* link partner ability                           */

/* LAN8742A vendor register: the resolved speed and duplex after negotiation,
 * which the standard registers do not report directly. */
#define PHY_SCSR     0x1Fu
#define PHY_SCSR_SPEED_Msk 0x001Cu

#define PHY_BSR_LINK_UP     (1u << 2)
#define PHY_BSR_AN_COMPLETE (1u << 5)

#define CADS_MDIO_TIMEOUT 100000u

void cads_hal_eth_mdio_init(void) {
    /* Management pins only. PA7 is deliberately left alone: it belongs to the
     * display until the RMII data path is actually brought up. */
    cads_gpio_init_alternate(GPIOA, 2u, 11u, CadsGpioPullNone); /* MDIO */
    cads_gpio_init_alternate(GPIOC, 1u, 11u, CadsGpioPullNone); /* MDC  */

    /* SYSCFG selects RMII vs MII, and must be set before the MAC clocks are
     * enabled or the choice does not take. */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->PMC |= SYSCFG_PMC_MII_RMII_SEL;

    RCC->AHB1ENR |= RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN;
    (void)RCC->AHB1ENR;

    /* MDC divider. HCLK is 180 MHz and MDC must stay under 2.5 MHz, so the
     * /102 divider (the largest available) gives 1.76 MHz. */
    ETH->MACMIIAR = (ETH->MACMIIAR & ~ETH_MACMIIAR_CR) | ETH_MACMIIAR_CR_Div102;
}

bool cads_hal_eth_mdio_read(uint8_t phy, uint8_t reg, uint16_t* value) {
    uint32_t control = ETH->MACMIIAR & ETH_MACMIIAR_CR; /* keep the divider */
    control |= ((uint32_t)phy << 11) & ETH_MACMIIAR_PA;
    control |= ((uint32_t)reg << 6) & ETH_MACMIIAR_MR;
    control |= ETH_MACMIIAR_MB; /* busy: starts the transfer */

    ETH->MACMIIAR = control;

    for(uint32_t guard = 0; guard < CADS_MDIO_TIMEOUT; guard++) {
        if(!(ETH->MACMIIAR & ETH_MACMIIAR_MB)) {
            *value = (uint16_t)ETH->MACMIIDR;
            return true;
        }
    }
    return false; /* no PHY at that address, or MDC is not running */
}

bool cads_hal_eth_mdio_write(uint8_t phy, uint8_t reg, uint16_t value) {
    ETH->MACMIIDR = value;

    uint32_t control = ETH->MACMIIAR & ETH_MACMIIAR_CR;
    control |= ((uint32_t)phy << 11) & ETH_MACMIIAR_PA;
    control |= ((uint32_t)reg << 6) & ETH_MACMIIAR_MR;
    control |= ETH_MACMIIAR_MW | ETH_MACMIIAR_MB;

    ETH->MACMIIAR = control;

    for(uint32_t guard = 0; guard < CADS_MDIO_TIMEOUT; guard++) {
        if(!(ETH->MACMIIAR & ETH_MACMIIAR_MB)) return true;
    }
    return false;
}

bool cads_hal_eth_phy_status(uint8_t phy, cads_eth_phy_status_t* status) {
    uint16_t id1, id2, bsr, scsr;

    if(!cads_hal_eth_mdio_read(phy, PHY_ID1, &id1)) return false;
    if(!cads_hal_eth_mdio_read(phy, PHY_ID2, &id2)) return false;

    /* An absent PHY reads as all ones (the line idles high) or all zeroes.
     * Either way the identifier is not a real OUI, and reporting "link down"
     * for a PHY that is not there would be actively misleading. */
    if((id1 == 0xFFFFu && id2 == 0xFFFFu) || (id1 == 0u && id2 == 0u)) return false;

    /* BSR latches link-down until read, so read it twice to get the current
     * state rather than the worst state since last time. */
    (void)cads_hal_eth_mdio_read(phy, PHY_BSR, &bsr);
    if(!cads_hal_eth_mdio_read(phy, PHY_BSR, &bsr)) return false;

    status->id1 = id1;
    status->id2 = id2;
    status->oui = ((uint32_t)id1 << 6) | ((uint32_t)(id2 >> 10) & 0x3Fu);
    status->model = (uint8_t)((id2 >> 4) & 0x3Fu);
    status->revision = (uint8_t)(id2 & 0x0Fu);
    status->bsr = bsr;
    status->link_up = (bsr & PHY_BSR_LINK_UP) != 0u;
    status->autoneg_done = (bsr & PHY_BSR_AN_COMPLETE) != 0u;

    status->speed_mbit = 0u;
    status->full_duplex = false;
    if(cads_hal_eth_mdio_read(phy, PHY_SCSR, &scsr)) {
        /* LAN8742A SCSR bits 4:2: 001 = 10 half, 101 = 10 full,
         * 010 = 100 half, 110 = 100 full. */
        uint16_t resolved = (uint16_t)((scsr & PHY_SCSR_SPEED_Msk) >> 2);
        switch(resolved) {
        case 0x1u: status->speed_mbit = 10u;  status->full_duplex = false; break;
        case 0x5u: status->speed_mbit = 10u;  status->full_duplex = true;  break;
        case 0x2u: status->speed_mbit = 100u; status->full_duplex = false; break;
        case 0x6u: status->speed_mbit = 100u; status->full_duplex = true;  break;
        default: break;
        }
    }
    return true;
}
