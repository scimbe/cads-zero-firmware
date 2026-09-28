/* rnlab L06 (DNS und NAT): host tests for l06_dns_nat_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L06. */

#include "unity.h"

#include "l06_dns_nat_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("dns-nat", rnlab_l06_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
