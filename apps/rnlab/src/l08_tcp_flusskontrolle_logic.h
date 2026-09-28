/*
 * CaDS Zero - rnlab L08 (TCP-Flusskontrolle): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l08.c links this file
 * directly on the host. Board integration lives in l08_tcp_flusskontrolle.c.
 */

#ifndef RNLAB_L08_TCP_FLUSSKONTROLLE_LOGIC_H
#define RNLAB_L08_TCP_FLUSSKONTROLLE_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("tcp-flusskontrolle") - placeholder until the lesson adds its own logic. */
const char* rnlab_l08_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L08_TCP_FLUSSKONTROLLE_LOGIC_H */
