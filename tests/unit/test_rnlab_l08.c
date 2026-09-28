/* rnlab L08 (TCP-Flusskontrolle): host tests for l08_tcp_flusskontrolle_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L08. */

#include "unity.h"

#include "l08_tcp_flusskontrolle_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("tcp-flusskontrolle", rnlab_l08_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
