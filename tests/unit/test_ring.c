/* cads_ring: the semantics targets/itsboard/hal/hal_console.c relies on. */

#include "unity.h"

#include "cads/toolbox/ring.h"

static uint8_t storage[8];
static cads_ring_t ring;

void setUp(void) {
    TEST_ASSERT_TRUE(cads_ring_init(&ring, storage, sizeof(storage)));
}

void tearDown(void) {
}

static void push_bytes(uint32_t count, uint8_t first) {
    for(uint32_t i = 0u; i < count; i++) {
        TEST_ASSERT_TRUE(cads_ring_push(&ring, (uint8_t)(first + i)));
    }
}

static void expect_bytes(uint32_t count, uint8_t first) {
    for(uint32_t i = 0u; i < count; i++) {
        uint8_t byte = 0u;
        TEST_ASSERT_TRUE(cads_ring_pop(&ring, &byte));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + i), byte);
    }
}

static void test_init_requires_a_power_of_two(void) {
    cads_ring_t other;
    uint8_t block[16];

    TEST_ASSERT_TRUE(cads_ring_init(&other, block, 16u));
    TEST_ASSERT_TRUE(cads_ring_init(&other, block, 2u));

    /* A size that is not a power of two would make the mask silently wrong,
     * which is the kind of bug that only shows up after the first wrap. */
    TEST_ASSERT_FALSE(cads_ring_init(&other, block, 12u));
    TEST_ASSERT_FALSE(cads_ring_init(&other, block, 1u));
    TEST_ASSERT_FALSE(cads_ring_init(&other, block, 0u));
    TEST_ASSERT_FALSE(cads_ring_init(&other, NULL, 16u));

    /* A rejected ring must be inert rather than half usable. */
    TEST_ASSERT_FALSE(cads_ring_init(&other, block, 3u));
    TEST_ASSERT_FALSE(cads_ring_push(&other, 0x41u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_capacity(&other));
    TEST_ASSERT_TRUE(cads_ring_is_empty(&other));
}

static void test_one_slot_is_spent_on_full_versus_empty(void) {
    TEST_ASSERT_EQUAL_UINT32(7u, cads_ring_capacity(&ring));
    TEST_ASSERT_EQUAL_UINT32(7u, cads_ring_space(&ring));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_count(&ring));
    TEST_ASSERT_TRUE(cads_ring_is_empty(&ring));
    TEST_ASSERT_FALSE(cads_ring_is_full(&ring));
}

static void test_empty_pop_leaves_the_output_untouched(void) {
    uint8_t byte = 0xA5u;
    TEST_ASSERT_FALSE(cads_ring_pop(&ring, &byte));
    TEST_ASSERT_EQUAL_UINT8(0xA5u, byte);
    TEST_ASSERT_FALSE(cads_ring_peek(&ring, &byte));
    TEST_ASSERT_EQUAL_UINT8(0xA5u, byte);
}

static void test_push_and_pop_preserve_order(void) {
    push_bytes(5u, 0x10u);
    TEST_ASSERT_EQUAL_UINT32(5u, cads_ring_count(&ring));
    TEST_ASSERT_EQUAL_UINT32(2u, cads_ring_space(&ring));

    expect_bytes(5u, 0x10u);
    TEST_ASSERT_TRUE(cads_ring_is_empty(&ring));
}

static void test_peek_does_not_consume(void) {
    push_bytes(2u, 0x55u);

    uint8_t byte = 0u;
    TEST_ASSERT_TRUE(cads_ring_peek(&ring, &byte));
    TEST_ASSERT_EQUAL_UINT8(0x55u, byte);
    TEST_ASSERT_TRUE(cads_ring_peek(&ring, &byte));
    TEST_ASSERT_EQUAL_UINT8(0x55u, byte);
    TEST_ASSERT_EQUAL_UINT32(2u, cads_ring_count(&ring));
}

static void test_full_ring_drops_the_newest_byte_and_counts_it(void) {
    push_bytes(7u, 0u);
    TEST_ASSERT_TRUE(cads_ring_is_full(&ring));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_space(&ring));

    TEST_ASSERT_FALSE(cads_ring_push(&ring, 0xFFu));
    TEST_ASSERT_FALSE(cads_ring_push(&ring, 0xFEu));
    TEST_ASSERT_EQUAL_UINT32(2u, cads_ring_dropped(&ring));

    /* The oldest bytes must have survived: a reader that loses the head of a
     * line gets a different command, not a truncated one. */
    TEST_ASSERT_EQUAL_UINT32(7u, cads_ring_count(&ring));
    expect_bytes(7u, 0u);
}

static void test_indices_wrap_without_losing_bytes(void) {
    /* Drive the head and tail several times around the buffer: everything
     * about a masked ring that can be wrong is wrong at the wrap. */
    uint8_t next = 0u;
    for(uint32_t round = 0u; round < 10u; round++) {
        push_bytes(5u, next);
        expect_bytes(3u, next);
        push_bytes(2u, (uint8_t)(next + 5u));
        expect_bytes(4u, (uint8_t)(next + 3u));
        next = (uint8_t)(next + 7u);
        TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_count(&ring));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_dropped(&ring));
}

static void test_bulk_write_reports_what_it_accepted(void) {
    static const uint8_t source[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t sink[10] = {0};

    /* Ten bytes into seven slots: the three that did not fit are drops, and
     * every one of them has to be counted or the total lies. */
    TEST_ASSERT_EQUAL_UINT32(7u, cads_ring_write(&ring, source, sizeof(source)));
    TEST_ASSERT_EQUAL_UINT32(3u, cads_ring_dropped(&ring));

    TEST_ASSERT_EQUAL_UINT32(7u, cads_ring_read(&ring, sink, sizeof(sink)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source, sink, 7);
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_read(&ring, sink, sizeof(sink)));
}

static void test_bulk_write_that_fits_drops_nothing(void) {
    static const uint8_t source[4] = {'C', 'a', 'D', 'S'};
    uint8_t sink[4] = {0};

    TEST_ASSERT_EQUAL_UINT32(4u, cads_ring_write(&ring, source, sizeof(source)));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_dropped(&ring));
    TEST_ASSERT_EQUAL_UINT32(2u, cads_ring_read(&ring, sink, 2u));
    TEST_ASSERT_EQUAL_UINT32(2u, cads_ring_read(&ring, &sink[2], 8u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source, sink, 4);
}

static void test_reset_clears_content_and_drop_count(void) {
    push_bytes(7u, 0u);
    TEST_ASSERT_FALSE(cads_ring_push(&ring, 0xFFu));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_ring_dropped(&ring));

    cads_ring_reset(&ring);
    TEST_ASSERT_TRUE(cads_ring_is_empty(&ring));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_count(&ring));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_dropped(&ring));
    TEST_ASSERT_EQUAL_UINT32(7u, cads_ring_space(&ring));
}

static void test_null_arguments_are_refused_rather_than_dereferenced(void) {
    uint8_t byte = 0u;
    TEST_ASSERT_FALSE(cads_ring_init(NULL, storage, 8u));
    TEST_ASSERT_FALSE(cads_ring_push(NULL, 0u));
    TEST_ASSERT_FALSE(cads_ring_pop(NULL, &byte));
    TEST_ASSERT_FALSE(cads_ring_pop(&ring, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_write(&ring, NULL, 4u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_read(&ring, NULL, 4u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ring_count(NULL));
    TEST_ASSERT_TRUE(cads_ring_is_empty(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_requires_a_power_of_two);
    RUN_TEST(test_one_slot_is_spent_on_full_versus_empty);
    RUN_TEST(test_empty_pop_leaves_the_output_untouched);
    RUN_TEST(test_push_and_pop_preserve_order);
    RUN_TEST(test_peek_does_not_consume);
    RUN_TEST(test_full_ring_drops_the_newest_byte_and_counts_it);
    RUN_TEST(test_indices_wrap_without_losing_bytes);
    RUN_TEST(test_bulk_write_reports_what_it_accepted);
    RUN_TEST(test_bulk_write_that_fits_drops_nothing);
    RUN_TEST(test_reset_clears_content_and_drop_count);
    RUN_TEST(test_null_arguments_are_refused_rather_than_dereferenced);
    return UNITY_END();
}
