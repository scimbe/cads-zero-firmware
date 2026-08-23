/* cads_snake: the arcade "Snake" game's own grid logic, tested with a
 * scripted rng and a small grid rather than the real panel/input - this
 * project has no way to feed a repeatable sequence of button presses into
 * a physically running game, so this is the only real proof the wall,
 * self-collision and eating rules are correct rather than assumed. */

#include <stdint.h>

#include "unity.h"

#include "cads/toolbox/snake.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_init_starts_length_one_moving_right(void) {
    cads_snake_t s;
    cads_snake_init(&s, 10u, 10u, 42u);

    TEST_ASSERT_EQUAL_UINT8(1u, s.length);
    TEST_ASSERT_EQUAL(CadsSnakeRight, s.dir);
    TEST_ASSERT_FALSE(s.game_over);
    TEST_ASSERT_EQUAL_UINT32(0u, s.score);
}

static void test_step_moves_the_head_one_cell(void) {
    cads_snake_t s;
    cads_snake_init(&s, 10u, 10u, 42u);
    uint8_t start_x = s.body_x[0];
    uint8_t start_y = s.body_y[0];

    cads_snake_step(&s, 1u);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)(start_x + 1u), s.body_x[0]);
    TEST_ASSERT_EQUAL_UINT8(start_y, s.body_y[0]);
    TEST_ASSERT_FALSE(s.game_over);
}

static void test_hitting_the_right_wall_ends_the_game(void) {
    cads_snake_t s;
    cads_snake_init(&s, 4u, 4u, 42u); /* head starts at (2,2), heading right */

    cads_snake_step(&s, 1u); /* (3,2) */
    TEST_ASSERT_FALSE(s.game_over);
    cads_snake_step(&s, 1u); /* would be (4,2): off the 4-wide grid */

    TEST_ASSERT_TRUE(s.game_over);
}

static void test_cannot_reverse_directly_into_the_neck(void) {
    cads_snake_t s;
    cads_snake_init(&s, 10u, 10u, 42u); /* heading right */

    cads_snake_set_direction(&s, CadsSnakeLeft); /* the 180-degree reversal */
    cads_snake_step(&s, 1u);

    /* Still heading right - the reversal was rejected, not merely delayed. */
    TEST_ASSERT_EQUAL(CadsSnakeRight, s.dir);
    TEST_ASSERT_FALSE(s.game_over);
}

static void test_eating_food_grows_and_scores(void) {
    cads_snake_t s;
    cads_snake_init(&s, 10u, 10u, 42u);
    /* Force food directly in front of the head so the very next step eats it. */
    s.food_x = (uint8_t)(s.body_x[0] + 1u);
    s.food_y = s.body_y[0];

    cads_snake_step(&s, 7u);

    TEST_ASSERT_EQUAL_UINT8(2u, s.length);
    TEST_ASSERT_EQUAL_UINT32(1u, s.score);
    TEST_ASSERT_FALSE(s.game_over);
}

static void test_food_never_spawns_on_the_snake(void) {
    cads_snake_t s;
    cads_snake_init(&s, 8u, 8u, 42u);

    /* Eat repeatedly (forcing food immediately ahead of the head, so every
     * step actually triggers a real cads_snake_place_food() call with a
     * different rng draw) and check on every single spawn that the food it
     * produced never lands on an occupied cell - the property
     * cads_snake_place_food()'s own scan exists for. Runs until the
     * straight-line growth reaches the wall. */
    for(uint32_t i = 0u; i < 10u && !s.game_over; i++) {
        uint8_t next_x = (uint8_t)(s.body_x[0] + 1u);
        if(next_x >= 8u) break; /* about to hit the wall, not the point of this test */
        s.food_x = next_x;
        s.food_y = s.body_y[0];
        cads_snake_step(&s, i * 2654435761u);

        for(uint8_t seg = 0u; seg < s.length; seg++) {
            bool on_snake = false;
            for(uint8_t other = 0u; other < s.length; other++) {
                if(other == seg) continue;
                if(s.body_x[other] == s.food_x && s.body_y[other] == s.food_y) on_snake = true;
            }
            TEST_ASSERT_FALSE(on_snake);
        }
    }
}

static void test_self_collision_ends_the_game(void) {
    cads_snake_t s;
    cads_snake_init(&s, 10u, 10u, 42u);

    /* Hand-built hook shape: head at (5,5), body doubling back through
     * (5,6) and (6,6), tail at (6,5). Heading the head Down drives it
     * directly onto body[1] - the segment right after the head, which
     * will NOT vacate this step (unlike the tail, body[3], which is
     * allowed to be moved onto) - the exact non-tail collision
     * cads_snake_step()'s check_len exists to still catch. */
    s.length = 4u;
    s.body_x[0] = 5u;
    s.body_y[0] = 5u;
    s.body_x[1] = 5u;
    s.body_y[1] = 6u;
    s.body_x[2] = 6u;
    s.body_y[2] = 6u;
    s.body_x[3] = 6u;
    s.body_y[3] = 5u;
    s.dir = CadsSnakeDown;
    s.pending_dir = CadsSnakeDown;
    s.food_x = 0u;
    s.food_y = 0u;

    cads_snake_step(&s, 1u);

    TEST_ASSERT_TRUE(s.game_over);
}

static void test_step_is_a_no_op_once_game_over(void) {
    cads_snake_t s;
    cads_snake_init(&s, 2u, 2u, 42u); /* tiny grid, one step hits a wall */
    cads_snake_step(&s, 1u);
    TEST_ASSERT_TRUE(s.game_over);

    uint8_t frozen_x = s.body_x[0];
    cads_snake_step(&s, 1u);

    TEST_ASSERT_EQUAL_UINT8(frozen_x, s.body_x[0]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_starts_length_one_moving_right);
    RUN_TEST(test_step_moves_the_head_one_cell);
    RUN_TEST(test_hitting_the_right_wall_ends_the_game);
    RUN_TEST(test_cannot_reverse_directly_into_the_neck);
    RUN_TEST(test_eating_food_grows_and_scores);
    RUN_TEST(test_food_never_spawns_on_the_snake);
    RUN_TEST(test_self_collision_ends_the_game);
    RUN_TEST(test_step_is_a_no_op_once_game_over);
    return UNITY_END();
}
