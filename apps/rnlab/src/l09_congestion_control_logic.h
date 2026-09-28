/*
 * CaDS Zero - rnlab L09 (Congestion Control): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l09.c links this file
 * directly on the host. Board integration lives in l09_congestion_control.c.
 */

#ifndef RNLAB_L09_CONGESTION_CONTROL_LOGIC_H
#define RNLAB_L09_CONGESTION_CONTROL_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("congestion-control") - placeholder until the lesson adds its own logic. */
const char* rnlab_l09_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L09_CONGESTION_CONTROL_LOGIC_H */
