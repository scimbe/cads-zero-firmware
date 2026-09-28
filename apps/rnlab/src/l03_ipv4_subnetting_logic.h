/*
 * CaDS Zero - rnlab L03 (IPv4 und Subnetting): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l03.c links this file
 * directly on the host. Board integration lives in l03_ipv4_subnetting.c.
 */

#ifndef RNLAB_L03_IPV4_SUBNETTING_LOGIC_H
#define RNLAB_L03_IPV4_SUBNETTING_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("ipv4-subnetting") - placeholder until the lesson adds its own logic. */
const char* rnlab_l03_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L03_IPV4_SUBNETTING_LOGIC_H */
