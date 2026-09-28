/*
 * CaDS Zero - rnlab L01 (Schichten und Kapselung): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l01.c links this file
 * directly on the host. Board integration lives in l01_schichten_kapselung.c.
 */

#ifndef RNLAB_L01_SCHICHTEN_KAPSELUNG_LOGIC_H
#define RNLAB_L01_SCHICHTEN_KAPSELUNG_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("schichten-kapselung") - placeholder until the lesson adds its own logic. */
const char* rnlab_l01_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L01_SCHICHTEN_KAPSELUNG_LOGIC_H */
