/*
 * CaDS Zero - auto-negotiation advertisement inspector for the LAN8742A.
 *
 * Registers 4 and 5 are IEEE 802.3 clause 22/28 standard registers - the same
 * layout on essentially every 10/100 PHY, not vendor-specific like the TDR
 * registers in hal_eth_tdr.c. Bit positions here are cross-checked against
 * ST's LAN8742 driver (stm32-lan8742/lan8742.h, same silicon) rather than
 * assumed from memory, matching this project's rule for register values:
 * transcribed, not guessed.
 */

#include "hal_eth_aneg.h"

#include "hal_eth_mdio.h"

/* Registers 4 and 5 share this layout, per IEEE 802.3 clause 28; bit 14 is
 * the one exception, reserved in ANAR and Acknowledge in ANLPAR, so it is
 * decoded separately below rather than folded into cads_eth_aneg_ability_t. */
#define PHY_ANAR   0x04u
#define PHY_ANLPAR 0x05u

#define ANEG_10_HALF      (1u << 5)
#define ANEG_10_FULL      (1u << 6)
#define ANEG_100_HALF     (1u << 7)
#define ANEG_100_FULL     (1u << 8)
#define ANEG_PAUSE        (1u << 10)
#define ANEG_PAUSE_ASYM   (1u << 11)
#define ANEG_REMOTE_FAULT (1u << 13)
#define ANEG_ACK          (1u << 14) /* ANLPAR only */

static cads_eth_aneg_ability_t cads_eth_aneg_decode(uint16_t raw) {
    cads_eth_aneg_ability_t ability;
    ability.half_10 = (raw & ANEG_10_HALF) != 0u;
    ability.full_10 = (raw & ANEG_10_FULL) != 0u;
    ability.half_100 = (raw & ANEG_100_HALF) != 0u;
    ability.full_100 = (raw & ANEG_100_FULL) != 0u;
    ability.pause = (raw & ANEG_PAUSE) != 0u;
    ability.pause_asym = (raw & ANEG_PAUSE_ASYM) != 0u;
    ability.remote_fault = (raw & ANEG_REMOTE_FAULT) != 0u;
    return ability;
}

/* IEEE 802.3 Table 28B-3 priority order, 100BASE-T4 omitted: the LAN8742A
 * never advertises it, so it can never be the resolved mode on this PHY. */
static cads_eth_aneg_mode_t cads_eth_aneg_resolve(
    const cads_eth_aneg_ability_t* local, const cads_eth_aneg_ability_t* partner) {
    if(local->full_100 && partner->full_100) return CadsEthAnegMode100Full;
    if(local->half_100 && partner->half_100) return CadsEthAnegMode100Half;
    if(local->full_10 && partner->full_10) return CadsEthAnegMode10Full;
    if(local->half_10 && partner->half_10) return CadsEthAnegMode10Half;
    return CadsEthAnegModeNone;
}

bool cads_hal_eth_aneg_report(uint8_t phy, cads_eth_aneg_report_t* report) {
    report->completed = false;

    uint16_t anar, anlpar;
    if(!cads_hal_eth_mdio_read(phy, PHY_ANAR, &anar)) return false;
    if(!cads_hal_eth_mdio_read(phy, PHY_ANLPAR, &anlpar)) return false;

    report->anar_raw = anar;
    report->anlpar_raw = anlpar;
    report->local = cads_eth_aneg_decode(anar);
    report->partner = cads_eth_aneg_decode(anlpar);
    report->partner_acknowledged = (anlpar & ANEG_ACK) != 0u;
    report->resolved = cads_eth_aneg_resolve(&report->local, &report->partner);
    report->completed = true;
    return true;
}
