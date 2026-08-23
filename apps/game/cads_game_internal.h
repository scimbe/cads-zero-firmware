/*
 * CaDS Zero - Leo's Arcade, private interface between the hub
 * (cads_game.c) and each mini-game's own glue file.
 *
 * Not installed publicly - cads_game.h is the only header anything
 * outside apps/game may include. Each mini-game owns its own state
 * struct, its own draw/input/tick and its own RNG (a local xorshift32,
 * seeded the same way the original reflex test already seeded one - see
 * cads_game_reflex.c's own header for why there is still no shared
 * cads/toolbox/rng.h). The actual game rules live in
 * cads/toolbox/{snake,breakout,dodger}.h; these files are only the
 * canvas/input glue around them, the same split every M5 watcher already
 * established between its toolbox parser and its explorer_*_demo.c.
 */

#ifndef CADS_GAME_INTERNAL_H
#define CADS_GAME_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "canvas.h"
#include "input/cads_input.h"

#include "cads/toolbox/breakout.h"
#include "cads/toolbox/dodger.h"
#include "cads/toolbox/snake.h"

/* --- reflex test (unchanged behaviour, moved out of the hub file) -------- */

typedef enum {
    CADS_GAME_REFLEX_IDLE = 0,
    CADS_GAME_REFLEX_ARMED,
    CADS_GAME_REFLEX_GO,
    CADS_GAME_REFLEX_TOO_SOON,
    CADS_GAME_REFLEX_RESULT,
} cads_game_reflex_state_t;

typedef struct {
    cads_game_reflex_state_t state;
    uint32_t state_entered_ms;
    uint32_t armed_delay_ms;
    uint32_t reaction_ms;
    uint32_t best_ms;
    uint32_t rounds_played;
    uint32_t rng_state;
} cads_game_reflex_t;

void cads_game_reflex_reset(cads_game_reflex_t* r, uint32_t now_ms);
/** Returns true when this call actually changed state - the hub only marks
 *  the view dirty then, rather than on every ~10ms tick regardless. */
bool cads_game_reflex_tick(cads_game_reflex_t* r, uint32_t now_ms);
void cads_game_reflex_draw(cads_rect_t area, const cads_game_reflex_t* r);
bool cads_game_reflex_input(const cads_input_event_t* event, cads_game_reflex_t* r);

/* --- snake ----------------------------------------------------------------- */

#define CADS_GAME_SNAKE_CELL 16
#define CADS_GAME_SNAKE_STEP_MS 150u

typedef struct {
    cads_snake_t snake;
    uint32_t rng_state;
    uint32_t last_step_ms;
    cads_rect_t area; /**< retained so a post-game-over restart does not need it re-passed in */
    uint8_t grid_width;
    uint8_t grid_height;
} cads_game_snake_t;

void cads_game_snake_reset(cads_game_snake_t* g, cads_rect_t area, uint32_t now_ms);
bool cads_game_snake_tick(cads_game_snake_t* g, cads_rect_t area, uint32_t now_ms);
void cads_game_snake_draw(cads_rect_t area, const cads_game_snake_t* g);
bool cads_game_snake_input(const cads_input_event_t* event, cads_game_snake_t* g, uint32_t now_ms);

/* --- breakout ---------------------------------------------------------------- */

#define CADS_GAME_BREAKOUT_STEP_MS 20u
#define CADS_GAME_BREAKOUT_PADDLE_STEP 4

typedef struct {
    cads_breakout_t breakout;
    uint32_t last_step_ms;
} cads_game_breakout_t;

void cads_game_breakout_reset(cads_game_breakout_t* g, cads_rect_t area, uint32_t now_ms);
bool cads_game_breakout_tick(cads_game_breakout_t* g, cads_rect_t area, uint32_t now_ms);
void cads_game_breakout_draw(cads_rect_t area, const cads_game_breakout_t* g);
bool cads_game_breakout_input(const cads_input_event_t* event, cads_game_breakout_t* g, uint32_t now_ms);

/* --- dodger ------------------------------------------------------------------ */

#define CADS_GAME_DODGER_STEP_MS 30u
#define CADS_GAME_DODGER_PLAYER_STEP 5

typedef struct {
    cads_dodger_t dodger;
    uint32_t rng_state;
    uint32_t last_step_ms;
} cads_game_dodger_t;

void cads_game_dodger_reset(cads_game_dodger_t* g, cads_rect_t area, uint32_t now_ms);
bool cads_game_dodger_tick(cads_game_dodger_t* g, cads_rect_t area, uint32_t now_ms);
void cads_game_dodger_draw(cads_rect_t area, const cads_game_dodger_t* g);
bool cads_game_dodger_input(const cads_input_event_t* event, cads_game_dodger_t* g, uint32_t now_ms);

#endif /* CADS_GAME_INTERNAL_H */
