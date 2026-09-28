/*
 * CaDS Zero - rnlab L02 (Ethernet und ARP): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l02.c links this file
 * directly on the host. Board integration lives in l02_ethernet_arp.c.
 */

#ifndef RNLAB_L02_ETHERNET_ARP_LOGIC_H
#define RNLAB_L02_ETHERNET_ARP_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("ethernet-arp") - placeholder until the lesson adds its own logic. */
const char* rnlab_l02_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L02_ETHERNET_ARP_LOGIC_H */
