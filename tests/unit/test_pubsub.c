/* cads_pubsub: the M2 service-registry line in docs/ROADMAP.md, the
 * publish/subscribe half. */

#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "cads/toolbox/pubsub.h"

void setUp(void) {
}

void tearDown(void) {
}

typedef struct {
    int last_value;
    int call_count;
} cads_test_listener_t;

static void cads_test_record(const void* message, void* context) {
    cads_test_listener_t* listener = (cads_test_listener_t*)context;
    listener->last_value = *(const int*)message;
    listener->call_count++;
}

static void test_publish_reaches_every_subscriber(void) {
    cads_pubsub_t pubsub;
    cads_pubsub_init(&pubsub);

    cads_pubsub_subscription_t sub_a, sub_b;
    cads_test_listener_t a = {0, 0}, b = {0, 0};

    cads_pubsub_subscribe(&pubsub, &sub_a, cads_test_record, &a);
    cads_pubsub_subscribe(&pubsub, &sub_b, cads_test_record, &b);

    int value = 42;
    cads_pubsub_publish(&pubsub, &value);

    TEST_ASSERT_EQUAL_INT(42, a.last_value);
    TEST_ASSERT_EQUAL_INT(1, a.call_count);
    TEST_ASSERT_EQUAL_INT(42, b.last_value);
    TEST_ASSERT_EQUAL_INT(1, b.call_count);
}

static void test_unsubscribe_stops_delivery(void) {
    cads_pubsub_t pubsub;
    cads_pubsub_init(&pubsub);

    cads_pubsub_subscription_t sub_a, sub_b;
    cads_test_listener_t a = {0, 0}, b = {0, 0};
    cads_pubsub_subscribe(&pubsub, &sub_a, cads_test_record, &a);
    cads_pubsub_subscribe(&pubsub, &sub_b, cads_test_record, &b);

    TEST_ASSERT_TRUE(cads_pubsub_unsubscribe(&pubsub, &sub_a));

    int value = 7;
    cads_pubsub_publish(&pubsub, &value);

    TEST_ASSERT_EQUAL_INT(0, a.call_count); /* never touched again */
    TEST_ASSERT_EQUAL_INT(1, b.call_count);
}

static void test_unsubscribe_twice_reports_the_second_as_a_no_op(void) {
    cads_pubsub_t pubsub;
    cads_pubsub_init(&pubsub);

    cads_pubsub_subscription_t sub;
    cads_test_listener_t a = {0, 0};
    cads_pubsub_subscribe(&pubsub, &sub, cads_test_record, &a);

    TEST_ASSERT_TRUE(cads_pubsub_unsubscribe(&pubsub, &sub));
    TEST_ASSERT_FALSE(cads_pubsub_unsubscribe(&pubsub, &sub));
}

static void test_unsubscribe_unknown_subscription_fails(void) {
    cads_pubsub_t pubsub;
    cads_pubsub_init(&pubsub);

    cads_pubsub_subscription_t never_subscribed;
    TEST_ASSERT_FALSE(cads_pubsub_unsubscribe(&pubsub, &never_subscribed));
}

static void test_publish_with_no_subscribers_does_nothing(void) {
    cads_pubsub_t pubsub;
    cads_pubsub_init(&pubsub);

    int value = 1;
    cads_pubsub_publish(&pubsub, &value); /* must not crash */
    TEST_ASSERT_TRUE(true);
}

static void test_null_arguments_are_refused(void) {
    cads_pubsub_t pubsub;
    cads_pubsub_init(&pubsub);
    cads_pubsub_subscription_t sub;

    cads_pubsub_subscribe(NULL, &sub, cads_test_record, NULL); /* must not crash */
    cads_pubsub_subscribe(&pubsub, NULL, cads_test_record, NULL);
    TEST_ASSERT_FALSE(cads_pubsub_unsubscribe(NULL, &sub));
    TEST_ASSERT_FALSE(cads_pubsub_unsubscribe(&pubsub, NULL));
    cads_pubsub_publish(NULL, NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_reaches_every_subscriber);
    RUN_TEST(test_unsubscribe_stops_delivery);
    RUN_TEST(test_unsubscribe_twice_reports_the_second_as_a_no_op);
    RUN_TEST(test_unsubscribe_unknown_subscription_fails);
    RUN_TEST(test_publish_with_no_subscribers_does_nothing);
    RUN_TEST(test_null_arguments_are_refused);
    return UNITY_END();
}
