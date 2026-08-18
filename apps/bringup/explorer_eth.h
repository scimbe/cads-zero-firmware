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

#endif /* CADS_EXPLORER_ETH_H */
