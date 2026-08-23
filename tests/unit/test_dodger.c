/* cads_dodger: the arcade "gap dodger" game's own scroll/collision logic,
 * tested with hand-placed obstacle positions rather than the real panel -
 * this project has no way to feed a repeatable button sequence into a
 * physically running game, so this is the only real proof the collision,
 * scoring and recycling rules are correct rather than assumed. */

#include <stdint.h>

#include "unity.h"

#include "cads/toolbox/dodger.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_init_spaces_obstacles_out_to_the_right(void) {
    cads_dodger_t d;
    cads_dodger_init(&d, 100, 100, 42u);

    TEST_ASSERT_EQUAL_UINT32(0u, d.score);
    TEST_ASSERT_FALSE(d.game_over);
    for(uint32_t i = 1u; i < CADS_DODGER_OBSTACLE_COUNT; i++) {
        TEST_ASSERT_TRUE(d.obstacle_x[i] > d.obstacle_x[i - 1u]);
        TEST_ASSERT_FALSE(d.obstacle_scored[i]);
    }
}

static void test_move_clamps_to_the_field(void) {
    cads_dodger_t d;
    cads_dodger_init(&d, 100, 100, 42u);

    cads_dodger_move(&d, -10000);
    TEST_ASSERT_EQUAL_INT16(0, d.player_y);

    cads_dodger_move(&d, 20000);
    TEST_ASSERT_EQUAL_INT16((int16_t)(100 - CADS_DODGER_PLAYER_SIZE), d.player_y);
}

static void test_step_scrolls_obstacles_left(void) {
    cads_dodger_t d;
    cads_dodger_init(&d, 200, 100, 42u);
    int16_t start_x = d.obstacle_x[0];

    cads_dodger_step(&d, 7u);

    TEST_ASSERT_EQUAL_INT16((int16_t)(start_x - CADS_DODGER_SPEED), d.obstacle_x[0]);
}

static void test_missing_the_gap_ends_the_game(void) {
    cads_dodger_t d;
    cads_dodger_init(&d, 100, 100, 42u);
    d.player_x = 20;
    d.player_y = 45; /* occupies [45, 55) */
    d.obstacle_x[0] = 15;
    d.obstacle_gap_y[0] = 60; /* gap [60, 130) - does not cover the player */
    d.obstacle_scored[0] = false;

    cads_dodger_step(&d, 1u);

    TEST_ASSERT_TRUE(d.game_over);
}

static void test_passing_through_the_gap_is_safe_and_scores_once(void) {
    cads_dodger_t d;
    cads_dodger_init(&d, 100, 100, 42u);
    d.player_x = 20;
    d.player_y = 45; /* occupies [45, 55) */
    d.obstacle_x[0] = 15;
    d.obstacle_gap_y[0] = 30; /* gap [30, 100) - fully covers the player */
    d.obstacle_scored[0] = false;

    for(uint32_t i = 0u; i < 4u; i++) {
        cads_dodger_step(&d, 1u);
        TEST_ASSERT_FALSE(d.game_over);
    }

    TEST_ASSERT_EQUAL_UINT32(1u, d.score);
    TEST_ASSERT_TRUE(d.obstacle_scored[0]);

    /* Further steps while it keeps scrolling left must not double-count. */
    cads_dodger_step(&d, 1u);
    TEST_ASSERT_EQUAL_UINT32(1u, d.score);
}

static void test_obstacle_recycles_after_leaving_the_field(void) {
    cads_dodger_t d;
    cads_dodger_init(&d, 100, 100, 42u);
    d.player_x = 20; /* far from where the obstacle recycles - no collision */
    d.obstacle_x[0] = -14;
    d.obstacle_scored[0] = true;

    cads_dodger_step(&d, 99u);

    TEST_ASSERT_EQUAL_INT16(100, d.obstacle_x[0]);
    TEST_ASSERT_FALSE(d.obstacle_scored[0]);
    TEST_ASSERT_TRUE(d.obstacle_gap_y[0] >= 0);
    TEST_ASSERT_TRUE(d.obstacle_gap_y[0] <= 100 - CADS_DODGER_GAP_HEIGHT);
}

static void test_step_is_a_no_op_once_game_over(void) {
    cads_dodger_t d;
    cads_dodger_init(&d, 100, 100, 42u);
    d.player_x = 20;
    d.player_y = 45;
    d.obstacle_x[0] = 15;
    d.obstacle_gap_y[0] = 60; /* not covering the player - guaranteed collision */
    cads_dodger_step(&d, 1u);
    TEST_ASSERT_TRUE(d.game_over);

    int16_t frozen_x = d.obstacle_x[0];
    cads_dodger_step(&d, 1u);

    TEST_ASSERT_EQUAL_INT16(frozen_x, d.obstacle_x[0]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_spaces_obstacles_out_to_the_right);
    RUN_TEST(test_move_clamps_to_the_field);
    RUN_TEST(test_step_scrolls_obstacles_left);
    RUN_TEST(test_missing_the_gap_ends_the_game);
    RUN_TEST(test_passing_through_the_gap_is_safe_and_scores_once);
    RUN_TEST(test_obstacle_recycles_after_leaving_the_field);
    RUN_TEST(test_step_is_a_no_op_once_game_over);
    return UNITY_END();
}
