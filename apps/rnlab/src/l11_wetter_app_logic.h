/*
 * CaDS Zero - rnlab L11 (Wetter-App): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l11.c links this file
 * directly on the host. Board integration lives in l11_wetter_app.c.
 */

#ifndef RNLAB_L11_WETTER_APP_LOGIC_H
#define RNLAB_L11_WETTER_APP_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("wetter-app") - placeholder until the lesson adds its own logic. */
const char* rnlab_l11_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L11_WETTER_APP_LOGIC_H */
