#ifndef CADS_EXPLORER_ETH_H
#define CADS_EXPLORER_ETH_H

#include <stdint.h>

/**
 * Print PHY identity and link state to the console, or explain why not.
 *
 * Board: reads the LAN8742A over MDIO (PA2/PC1), which never touches PA7 and
 * so never disturbs the display. Host: there is no PHY to read, and the
 * command says so rather than fabricating a status.
 */
void cads_explorer_eth_status(void);

/**
 * Run cable diagnostics (TDR on MDI + MDIX, plus matched-length if the link
 * is up before the test) and print the results.
 *
 * DISRUPTIVE: forces the PHY out of auto-negotiation for the duration of the
 * TDR portion. The prior state is restored afterwards and the link
 * renegotiates, but anything depending on that link sees a brief drop.
 */
void cads_explorer_eth_cable_test(void);

/** Decode and print ANAR/ANLPAR: what each side advertised, and the
 *  resolved highest-common-denominator mode. Non-disruptive, MDIO-only. */
void cads_explorer_eth_aneg(void);

/**
 * Poll the link event log once and print any new events, then print the
 * whole log. Call repeatedly (e.g. via the `w` pattern) to watch a link
 * being unplugged/replugged in real time. MDIO-only.
 */
void cads_explorer_eth_linklog_poll_and_dump(void);

/** Read and print the MAC's six hardware traffic counters. Non-disruptive,
 *  no MDIO involved - direct MAC register access. */
void cads_explorer_eth_mmc(void);

/**
 * M5 bring-up gate: cads_net_init() once, then poll for `seconds` (default
 * 20) and report the netif's own rx/tx/dropped counters alongside the MAC's
 * hardware MMC counters (docs/ROADMAP.md's "MMC-counter-based traffic
 * verification").
 *
 * A nonzero rx delta with the board plugged into a live switch needs no
 * traffic generator: ambient broadcast/multicast frames (ARP, mDNS, STP...)
 * arrive on their own, and seeing them come up through cads_net_status()
 * as well as the MAC's own counters proves the whole path - descriptor
 * rings, DMA, ethernet_input(), lwIP - moved a real frame, not just that
 * the MAC counted one. Board: real netif. Host: says so, same as the rest
 * of this file.
 */
void cads_explorer_net_test(uint32_t seconds);

/**
 * The fixed, locally-administered MAC address this firmware calls
 * cads_net_init() with, everywhere it is called (`h` above,
 * apps/bringup/explorer_app_demo.c) - one source of truth, since
 * cads_net_init() ignores the address on every call after the first and two
 * different constants would make behaviour depend on which caller happened
 * to run first. Board only, like the rest of this file; the sim never calls
 * cads_net_init() with a real intent to bring anything up.
 */
const uint8_t* cads_explorer_net_mac(void);

#endif /* CADS_EXPLORER_ETH_H */
