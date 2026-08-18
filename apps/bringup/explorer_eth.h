#ifndef CADS_EXPLORER_ETH_H
#define CADS_EXPLORER_ETH_H

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

#endif /* CADS_EXPLORER_ETH_H */
