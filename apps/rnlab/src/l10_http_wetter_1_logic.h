/*
 * CaDS Zero - rnlab L10 (HTTP-Client (Wetter 1)): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l10.c links this file
 * directly on the host. Board integration lives in l10_http_wetter_1.c.
 */

#ifndef RNLAB_L10_HTTP_WETTER_1_LOGIC_H
#define RNLAB_L10_HTTP_WETTER_1_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("http-wetter-1") - placeholder until the lesson adds its own logic. */
const char* rnlab_l10_slug(void);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L10_HTTP_WETTER_1_LOGIC_H */
