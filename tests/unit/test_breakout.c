/* cads_breakout: the arcade "Breakout" game's own ball/paddle/brick
 * physics, tested with hand-placed positions rather than the real panel -
 * this project has no way to feed a repeatable ball trajectory into a
 * physically running game, so this is the only real proof the wall,
 * brick, paddle and miss rules are correct rather than assumed. */

#include <stdint.h>

#include "unity.h"

#include "cads/toolbox/breakout.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_init_starts_with_every_brick_standing(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100);

    for(size_t i = 0u; i < CADS_BREAKOUT_BRICK_COUNT; i++) {
        TEST_ASSERT_TRUE(b.bricks[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(CADS_BREAKOUT_START_LIVES, b.lives);
    TEST_ASSERT_EQUAL_UINT32(0u, b.score);
    TEST_ASSERT_FALSE(b.game_over);
    TEST_ASSERT_FALSE(b.cleared);
}

static void test_ball_bounces_off_the_left_wall(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100);
    b.ball_x = 1;
    b.ball_y = 50; /* clear of the brick band and the paddle */
    b.vel_x = -3;
    b.vel_y = 0;

    cads_breakout_step(&b);

    TEST_ASSERT_EQUAL_INT16(0, b.ball_x);
    TEST_ASSERT_EQUAL_INT16(3, b.vel_x);
}

static void test_ball_bounces_off_the_right_wall(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100);
    b.ball_x = 78;
    b.ball_y = 50;
    b.vel_x = 3;
    b.vel_y = 0;

    cads_breakout_step(&b);

    TEST_ASSERT_EQUAL_INT16(79, b.ball_x);
    TEST_ASSERT_EQUAL_INT16(-3, b.vel_x);
}

static void test_hitting_a_brick_clears_it_and_scores(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100); /* 10px/col, brick band y < 30 */
    b.ball_x = 5;  /* column 0 */
    b.ball_y = 25; /* row 2, still inside the brick band after moving up 1px */
    b.vel_x = 0;
    b.vel_y = -2;

    cads_breakout_step(&b);

    TEST_ASSERT_FALSE(b.bricks[2u * CADS_BREAKOUT_COLS + 0u]);
    TEST_ASSERT_EQUAL_UINT32(1u, b.score);
    TEST_ASSERT_EQUAL_INT16(2, b.vel_y); /* bounced back down */
    TEST_ASSERT_FALSE(b.game_over);
}

static void test_paddle_catches_the_ball(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100); /* paddle_width=13, paddle_x=33, paddle_y=94 */
    b.ball_x = 40;                   /* inside [33, 46] */
    b.ball_y = 93;
    b.vel_x = 0;
    b.vel_y = 2;

    cads_breakout_step(&b);

    TEST_ASSERT_EQUAL_INT16(94, b.ball_y);
    TEST_ASSERT_EQUAL_INT16(-2, b.vel_y);
    TEST_ASSERT_FALSE(b.game_over);
    TEST_ASSERT_EQUAL_UINT32(CADS_BREAKOUT_START_LIVES, b.lives);
}

static void test_missing_the_paddle_costs_a_life_and_reserves(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100); /* paddle spans [33, 46] */
    b.ball_x = 60;                   /* well outside the paddle */
    b.ball_y = 97;
    b.vel_x = 0;
    b.vel_y = 5;

    cads_breakout_step(&b);

    TEST_ASSERT_EQUAL_UINT32(CADS_BREAKOUT_START_LIVES - 1u, b.lives);
    TEST_ASSERT_FALSE(b.game_over);
    /* Re-served: back inside the field, not still past the bottom edge. */
    TEST_ASSERT_TRUE(b.ball_y < 100);
}

static void test_losing_every_life_ends_the_game(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100);

    for(uint32_t miss = 0u; miss < CADS_BREAKOUT_START_LIVES; miss++) {
        b.ball_x = 60; /* outside the paddle every time */
        b.ball_y = 97;
        b.vel_x = 0;
        b.vel_y = 5;
        cads_breakout_step(&b);
    }

    TEST_ASSERT_EQUAL_UINT32(0u, b.lives);
    TEST_ASSERT_TRUE(b.game_over);
}

static void test_clearing_the_last_brick_wins(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100);
    for(size_t i = 1u; i < CADS_BREAKOUT_BRICK_COUNT; i++) b.bricks[i] = false;
    TEST_ASSERT_TRUE(b.bricks[0]); /* the one brick left standing */

    b.ball_x = 5;
    b.ball_y = 5;
    b.vel_x = 0;
    b.vel_y = -1;

    cads_breakout_step(&b);

    TEST_ASSERT_FALSE(b.bricks[0]);
    TEST_ASSERT_TRUE(b.cleared);
    TEST_ASSERT_TRUE(b.game_over);
}

static void test_step_is_a_no_op_once_game_over(void) {
    cads_breakout_t b;
    cads_breakout_init(&b, 80, 100);
    b.lives = 1u;
    b.ball_x = 60;
    b.ball_y = 97;
    b.vel_x = 0;
    b.vel_y = 5;
    cads_breakout_step(&b);
    TEST_ASSERT_TRUE(b.game_over);

    int16_t frozen_x = b.ball_x;
    cads_breakout_step(&b);

    TEST_ASSERT_EQUAL_INT16(frozen_x, b.ball_x);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_starts_with_every_brick_standing);
    RUN_TEST(test_ball_bounces_off_the_left_wall);
    RUN_TEST(test_ball_bounces_off_the_right_wall);
    RUN_TEST(test_hitting_a_brick_clears_it_and_scores);
    RUN_TEST(test_paddle_catches_the_ball);
    RUN_TEST(test_missing_the_paddle_costs_a_life_and_reserves);
    RUN_TEST(test_losing_every_life_ends_the_game);
    RUN_TEST(test_clearing_the_last_brick_wins);
    RUN_TEST(test_step_is_a_no_op_once_game_over);
    return UNITY_END();
}
