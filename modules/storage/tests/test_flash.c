/* cads/storage/flash.h against src/cads_flash_host.c - the RAM-backed
 * emulation described in that file's own header. Board-only behaviour (real
 * erase timing, the .ramfunc placement, the two-independent-bounds-check
 * design in cads_flash_stm32f4.c) is reviewed by reading, not tested here:
 * there is no way to exercise real flash electrical behaviour on a host CI
 * runner, and pretending otherwise would be worse than not testing it. What
 * this DOES cover - geometry, bounds, alignment, and NOR "program only
 * clears bits" semantics - is exactly what both drivers are required to
 * agree on. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/storage/flash.h"

void setUp(void) {
    cads_flash_init();
}

void tearDown(void) {
}

static void test_geometry_matches_the_documented_window(void) {
    const cads_flash_geometry_t* geom = cads_flash_geometry();
    TEST_ASSERT_NOT_NULL(geom);
    TEST_ASSERT_EQUAL_UINT32(0x08120000u, geom->base);
    TEST_ASSERT_EQUAL_UINT32(896u * 1024u, geom->size);
    TEST_ASSERT_EQUAL_UINT32(128u * 1024u, geom->block_size);
    TEST_ASSERT_EQUAL_UINT32(7u, geom->block_count);
    TEST_ASSERT_EQUAL_UINT32(geom->block_size * geom->block_count, geom->size);
}

static void test_erase_sets_the_block_to_all_ones(void) {
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_erase_block(0));

    uint8_t buffer[64];
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_read(0, buffer, sizeof(buffer)));
    for(size_t i = 0; i < sizeof(buffer); i++) {
        TEST_ASSERT_EQUAL_UINT8(0xFFu, buffer[i]);
    }
}

static void test_program_then_read_round_trips(void) {
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_erase_block(0));

    uint8_t written[16];
    for(size_t i = 0; i < sizeof(written); i++) written[i] = (uint8_t)(i * 3u);
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_program(0, written, sizeof(written)));

    uint8_t read_back[16] = {0};
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_read(0, read_back, sizeof(read_back)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(written, read_back, sizeof(written));
}

static void test_program_over_live_data_only_clears_bits(void) {
    /* This is the NOR property the whole driver contract depends on: a
     * program can turn a 1 into a 0 but never a 0 back into a 1. Programming
     * over data that was never erased silently ANDs the two - which means
     * the second program below does NOT actually produce `second`, and the
     * driver's own post-write verify is documented to catch exactly that
     * (cads/storage/flash.h: "the driver reads back what it wrote and
     * returns CADS_FLASH_ERR_VERIFY when they differ"). The interesting
     * assertion here is not that the call fails, but *what ends up in
     * flash* despite the failure - the AND, not a silently discarded write. */
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_erase_block(0));

    uint32_t first = 0xFFFFFF00u;
    uint32_t second = 0x0000FFFFu;
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_program(0, &first, sizeof(first)));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_VERIFY, cads_flash_program(0, &second, sizeof(second)));

    uint32_t result = 0;
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_read(0, &result, sizeof(result)));
    TEST_ASSERT_EQUAL_HEX32(first & second, result);
}

static void test_range_beyond_the_window_is_refused(void) {
    uint8_t buffer[8];
    const cads_flash_geometry_t* geom = cads_flash_geometry();

    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_RANGE, cads_flash_read(geom->size, buffer, 4u));
    /* Last aligned offset before the end, reading 8 bytes - unit-aligned on
     * both ends, but the read still runs 4 bytes past the window. */
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_RANGE, cads_flash_read(geom->size - 4u, buffer, 8u));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_RANGE, cads_flash_program(geom->size, buffer, 4u));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_RANGE, cads_flash_erase_block(geom->block_count));
}

static void test_zero_size_is_refused_not_a_silent_no_op(void) {
    uint8_t buffer[4] = {0};
    /* A zero-size call that quietly "succeeded" would hide a caller's bug -
     * an off-by-one that computed a zero length looks identical to doing
     * nothing on purpose unless this is a hard error. Which error differs
     * by function: read() checks "no buffer or zero size" as one argument
     * error before it ever reaches the window check; program() has no
     * explicit zero-size check of its own and falls through to the same
     * window check cads_flash_in_window() gives every other range violation,
     * which treats size 0 as never "in window". Different code path, same
     * outcome: neither call can be mistaken for a no-op success. */
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_ARG, cads_flash_read(0, buffer, 0u));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_RANGE, cads_flash_program(0, buffer, 0u));
}

static void test_misaligned_offset_or_size_is_refused(void) {
    uint8_t buffer[8] = {0};
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_ALIGN, cads_flash_read(1u, buffer, 4u));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_ALIGN, cads_flash_read(0u, buffer, 3u));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_ALIGN, cads_flash_program(2u, buffer, 4u));
}

static void test_null_buffer_is_refused(void) {
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_ARG, cads_flash_read(0u, NULL, 4u));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_ERR_ARG, cads_flash_program(0u, NULL, 4u));
}

static void test_erase_touches_only_its_own_block(void) {
    uint32_t pattern = 0x12345678u;
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_erase_block(0));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_erase_block(1));
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_program(0, &pattern, sizeof(pattern)));

    const cads_flash_geometry_t* geom = cads_flash_geometry();
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_erase_block(1)); /* re-erase the neighbour */

    uint32_t still_there = 0;
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_read(0, &still_there, sizeof(still_there)));
    TEST_ASSERT_EQUAL_HEX32(pattern, still_there);

    uint32_t neighbour_word = 0;
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_read(geom->block_size, &neighbour_word, sizeof(neighbour_word)));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, neighbour_word);
}

static void test_init_is_idempotent(void) {
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_init());
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_init());
    const cads_flash_geometry_t* first = cads_flash_geometry();
    TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_init());
    TEST_ASSERT_EQUAL_PTR(first, cads_flash_geometry()); /* same storage, not reinitialised */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_geometry_matches_the_documented_window);
    RUN_TEST(test_erase_sets_the_block_to_all_ones);
    RUN_TEST(test_program_then_read_round_trips);
    RUN_TEST(test_program_over_live_data_only_clears_bits);
    RUN_TEST(test_range_beyond_the_window_is_refused);
    RUN_TEST(test_zero_size_is_refused_not_a_silent_no_op);
    RUN_TEST(test_misaligned_offset_or_size_is_refused);
    RUN_TEST(test_null_buffer_is_refused);
    RUN_TEST(test_erase_touches_only_its_own_block);
    RUN_TEST(test_init_is_idempotent);
    return UNITY_END();
}
