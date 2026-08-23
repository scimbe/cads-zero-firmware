/*
 * CaDS Zero toolbox - auto-scrolling gap-dodging game logic.
 *
 * Pure state machine over a caller-owned playfield, same convention as
 * cads/toolbox/snake.h and breakout.h: no HAL, no canvas, no clock. The
 * player sits at a fixed x column; obstacles - vertical bars with one
 * gap each - scroll toward it from the right. Miss the gap, it is over;
 * clear it, the score goes up and that obstacle recycles off the right
 * edge with a new random gap.
 */

#ifndef CADS_TOOLBOX_DODGER_H
#define CADS_TOOLBOX_DODGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Three in flight at once is enough that the next gap is always visible
 * before the current one arrives, without carrying more state than a
 * small screen can even show at once. */
#define CADS_DODGER_OBSTACLE_COUNT 3u
#define CADS_DODGER_OBSTACLE_WIDTH 16
#define CADS_DODGER_GAP_HEIGHT 70
#define CADS_DODGER_SPEED 3 /**< pixels per cads_dodger_step() call */
#define CADS_DODGER_PLAYER_SIZE 10 /**< the player is drawn/collided as this square */

typedef struct {
    int16_t field_width;
    int16_t field_height;
    int16_t player_x; /**< fixed column */
    int16_t player_y;
    int16_t obstacle_x[CADS_DODGER_OBSTACLE_COUNT]; /**< left edge */
    int16_t obstacle_gap_y[CADS_DODGER_OBSTACLE_COUNT]; /**< top of the gap */
    bool obstacle_scored[CADS_DODGER_OBSTACLE_COUNT];
    uint32_t score;
    bool game_over;
} cads_dodger_t;

/** Start a fresh game: player centred, obstacles spaced out to the right. */
void cads_dodger_init(cads_dodger_t* d, int16_t field_width, int16_t field_height, uint32_t rng);

/** Move the player by `dy` pixels, clamped to stay inside the field. */
void cads_dodger_move(cads_dodger_t* d, int16_t dy);

/**
 * Scroll every obstacle left by CADS_DODGER_SPEED, check the player's
 * column against whichever obstacle currently overlaps it, score once per
 * obstacle successfully passed, and recycle an obstacle (with a fresh
 * random gap) once it scrolls fully off the left edge. No-op once
 * `game_over` is set.
 */
void cads_dodger_step(cads_dodger_t* d, uint32_t rng);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_DODGER_H */
