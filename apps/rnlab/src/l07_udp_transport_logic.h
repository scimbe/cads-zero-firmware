/*
 * CaDS Zero - rnlab L07 (UDP-Transport): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l07.c links this file
 * directly on the host. Board integration lives in l07_udp_transport.c.
 */

#ifndef RNLAB_L07_UDP_TRANSPORT_LOGIC_H
#define RNLAB_L07_UDP_TRANSPORT_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("udp-transport") - placeholder until the lesson adds its own logic. */
const char* rnlab_l07_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L07_UDP_TRANSPORT_LOGIC_H */
