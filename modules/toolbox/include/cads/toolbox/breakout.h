/*
 * CaDS Zero toolbox - paddle/ball/brick game logic.
 *
 * Pure state machine over a caller-owned playfield, same convention as
 * cads/toolbox/snake.h: no HAL, no canvas, no clock. Coordinates are in
 * the caller's own pixel space (whatever `field_width`/`field_height` it
 * inits with) so apps/game can hand this the exact play area it lays out
 * on the panel without a unit conversion at the boundary.
 *
 * The ball is treated as a point, not a circle - a filled few-pixel square
 * is the caller's business when drawing it, and point collision is exact
 * and cheap, unlike circle-vs-rect math this game has no need for.
 */

#ifndef CADS_TOOLBOX_BREAKOUT_H
#define CADS_TOOLBOX_BREAKOUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CADS_BREAKOUT_COLS 8u
#define CADS_BREAKOUT_ROWS 3u
#define CADS_BREAKOUT_BRICK_COUNT (CADS_BREAKOUT_COLS * CADS_BREAKOUT_ROWS)
#define CADS_BREAKOUT_BRICK_ROW_HEIGHT 10
#define CADS_BREAKOUT_PADDLE_HEIGHT 6
#define CADS_BREAKOUT_START_LIVES 3u

typedef struct {
    int16_t field_width;
    int16_t field_height;
    int16_t paddle_x; /**< left edge */
    int16_t paddle_width;
    int16_t ball_x;
    int16_t ball_y;
    int16_t vel_x; /**< pixels per cads_breakout_step() call, never 0 */
    int16_t vel_y;
    bool bricks[CADS_BREAKOUT_BRICK_COUNT]; /**< true = still standing, row-major */
    uint32_t score;
    uint8_t lives;
    bool game_over;
    bool cleared; /**< every brick gone - a win, also ends the game */
} cads_breakout_t;

/** Start a fresh game: full brick wall, centred paddle, ball served upward. */
void cads_breakout_init(cads_breakout_t* b, int16_t field_width, int16_t field_height);

/** Move the paddle to `x` (left edge), clamped to stay inside the field. */
void cads_breakout_set_paddle_x(cads_breakout_t* b, int16_t x);

/**
 * Advance the ball one step: moves it, bounces off the side/top walls, the
 * paddle and any standing brick it touches (clearing that brick and
 * scoring), and re-serves it (losing a life) if it passes the paddle.
 * No-op once `game_over` is set.
 */
void cads_breakout_step(cads_breakout_t* b);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_BREAKOUT_H */
