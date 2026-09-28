/* rnlab L11 (Wetter-App): host tests for l11_wetter_app_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L11. */

#include "unity.h"

#include "l11_wetter_app_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("wetter-app", rnlab_l11_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
