/* rnlab L01 (Schichten und Kapselung): host tests for l01_schichten_kapselung_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L01. */

#include "unity.h"

#include "l01_schichten_kapselung_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("schichten-kapselung", rnlab_l01_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
