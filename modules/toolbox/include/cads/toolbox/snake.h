/*
 * CaDS Zero toolbox - grid-based snake game logic.
 *
 * Pure state machine over a caller-owned grid: no HAL, no canvas, no
 * clock of its own - the same convention as cads/toolbox/mactable.h and
 * every M5 watcher this project already has. The caller (apps/game) owns
 * the tick interval, the RNG (a local xorshift32, same as
 * apps/game/cads_game.c's existing reflex-test RNG - nothing here needed
 * a second one) and the canvas drawing; this file only knows grid cells,
 * never pixels.
 *
 * `rng` is passed in per call rather than kept as internal state, so a
 * test can hand this a scripted sequence and get a reproducible board -
 * the same reason every M5 watcher's own parser takes caller-supplied
 * bytes instead of reading a live capture itself.
 */

#ifndef CADS_TOOLBOX_SNAKE_H
#define CADS_TOOLBOX_SNAKE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 64 segments is already longer than a snake on a modest grid can become
 * before it fills most of the board; capping it here, not growing it
 * unbounded, is what keeps this struct's size fixed and small. */
#define CADS_SNAKE_MAX_LENGTH 64u

typedef enum {
    CadsSnakeUp = 0,
    CadsSnakeDown,
    CadsSnakeLeft,
    CadsSnakeRight,
} cads_snake_dir_t;

typedef struct {
    uint8_t body_x[CADS_SNAKE_MAX_LENGTH]; /**< [0] is the head */
    uint8_t body_y[CADS_SNAKE_MAX_LENGTH];
    uint8_t length;
    cads_snake_dir_t dir;
    cads_snake_dir_t pending_dir; /**< latched; committed on the next cads_snake_step() */
    uint8_t food_x;
    uint8_t food_y;
    uint8_t grid_width;
    uint8_t grid_height;
    uint32_t score;
    bool game_over;
} cads_snake_t;

/** Start a fresh game: length-1 snake centred on the grid, moving right. */
void cads_snake_init(cads_snake_t* s, uint8_t grid_width, uint8_t grid_height, uint32_t rng);

/**
 * Change heading. Rejects the 180-degree reversal (turning directly into
 * the segment behind the head) the way every Snake does - accepting it
 * would just be an instant, confusing game over. Takes effect on the
 * next cads_snake_step(), not immediately - a step already in flight this
 * tick keeps its old heading.
 */
void cads_snake_set_direction(cads_snake_t* s, cads_snake_dir_t dir);

/**
 * Advance one grid cell in the current heading. Handles wall collision,
 * self collision, eating (grows by one, respawns food, +1 score), and
 * capping growth at CADS_SNAKE_MAX_LENGTH. No-op once `game_over` is set
 * - the caller decides when to cads_snake_init() again.
 *
 * `rng` is only consumed when food needs to (re)spawn; a step that does
 * not eat ignores it entirely, so a caller cannot tell from the return
 * alone whether this value mattered.
 */
void cads_snake_step(cads_snake_t* s, uint32_t rng);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_SNAKE_H */
