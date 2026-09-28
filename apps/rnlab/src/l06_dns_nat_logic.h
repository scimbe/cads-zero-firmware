/*
 * CaDS Zero - rnlab L06 (DNS und NAT): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l06.c links this file
 * directly on the host. Board integration lives in l06_dns_nat.c.
 */

#ifndef RNLAB_L06_DNS_NAT_LOGIC_H
#define RNLAB_L06_DNS_NAT_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("dns-nat") - placeholder until the lesson adds its own logic. */
const char* rnlab_l06_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L06_DNS_NAT_LOGIC_H */
