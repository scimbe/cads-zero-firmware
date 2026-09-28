/*
 * CaDS Zero - rnlab L04 (ICMP): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l04.c links this file
 * directly on the host. Board integration lives in l04_icmp.c.
 */

#ifndef RNLAB_L04_ICMP_LOGIC_H
#define RNLAB_L04_ICMP_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("icmp") - placeholder until the lesson adds its own logic. */
const char* rnlab_l04_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L04_ICMP_LOGIC_H */
