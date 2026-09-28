/* rnlab L10 (HTTP-Client (Wetter 1)): host tests for l10_http_wetter_1_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L10. */

#include "unity.h"

#include "l10_http_wetter_1_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("http-wetter-1", rnlab_l10_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
