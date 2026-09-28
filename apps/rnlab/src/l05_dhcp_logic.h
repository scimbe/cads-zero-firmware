/*
 * CaDS Zero - rnlab L05 (DHCP): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l05.c links this file
 * directly on the host. Board integration lives in l05_dhcp.c.
 */

#ifndef RNLAB_L05_DHCP_LOGIC_H
#define RNLAB_L05_DHCP_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("dhcp") - placeholder until the lesson adds its own logic. */
const char* rnlab_l05_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L05_DHCP_LOGIC_H */
