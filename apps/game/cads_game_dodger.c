/*
 * CaDS Zero - Dodger, one cartridge in Leo's Arcade.
 *
 * The obstacle scroll/collision logic lives in cads/toolbox/dodger.h and
 * is unit-tested there (tests/unit/test_dodger.c); this file is only the
 * pixel-space setup, score/game-over overlay, and turning Up/Down into
 * player movement. Polled rather than event driven for the same reason
 * cads_game_breakout.c's paddle is - see that file's own header.
 */

#include "cads_game_internal.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"

static void cads_game_dodger_start(cads_game_dodger_t* g, int16_t field_width, int16_t field_height, uint32_t now_ms) {
    g->rng_state = (uint32_t)cads_hal_ticks_us();
    if(g->rng_state == 0u) g->rng_state = 0x9E3779B9u;
    cads_dodger_init(&g->dodger, field_width, field_height, g->rng_state);
    g->last_step_ms = now_ms;
}

void cads_game_dodger_reset(cads_game_dodger_t* g, cads_rect_t area, uint32_t now_ms) {
    cads_game_dodger_start(g, area.width, area.height, now_ms);
}

static uint32_t cads_game_dodger_rand(cads_game_dodger_t* g) {
    uint32_t x = g->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng_state = x;
    return x;
}

bool cads_game_dodger_tick(cads_game_dodger_t* g, cads_rect_t area, uint32_t now_ms) {
    (void)area;
    if(g->dodger.game_over) return false;
    if(now_ms - g->last_step_ms < CADS_GAME_DODGER_STEP_MS) return false;
    g->last_step_ms = now_ms;

    if(cads_input_is_down(CadsKeyUp)) cads_dodger_move(&g->dodger, (int16_t)(-CADS_GAME_DODGER_PLAYER_STEP));
    if(cads_input_is_down(CadsKeyDown)) cads_dodger_move(&g->dodger, CADS_GAME_DODGER_PLAYER_STEP);

    cads_dodger_step(&g->dodger, cads_game_dodger_rand(g));
    return true;
}

void cads_game_dodger_draw(cads_rect_t area, const cads_game_dodger_t* g) {
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorWhite);

    for(uint32_t i = 0u; i < CADS_DODGER_OBSTACLE_COUNT; i++) {
        int16_t x = (int16_t)(area.x + g->dodger.obstacle_x[i]);
        int16_t gap_top = (int16_t)(area.y + g->dodger.obstacle_gap_y[i]);
        int16_t gap_bottom = (int16_t)(gap_top + CADS_DODGER_GAP_HEIGHT);
        cads_canvas_fill_rect(x, area.y, CADS_DODGER_OBSTACLE_WIDTH, (int16_t)(gap_top - area.y), CadsColorGrayDark);
        cads_canvas_fill_rect(
            x, gap_bottom, CADS_DODGER_OBSTACLE_WIDTH, (int16_t)(area.y + area.height - gap_bottom), CadsColorGrayDark);
    }

    cads_canvas_fill_rect(
        (int16_t)(area.x + g->dodger.player_x), (int16_t)(area.y + g->dodger.player_y), CADS_DODGER_PLAYER_SIZE,
        CADS_DODGER_PLAYER_SIZE, CadsColorBrand);

    char score_text[24];
    size_t pos = cads_str_copy(score_text, sizeof(score_text), "Score: ");
    cads_fmt_uint(score_text + pos, sizeof(score_text) - pos, g->dodger.score);
    cads_rect_t header = {area.x, area.y, area.width, 18};
    cads_canvas_draw_text_aligned(header, CadsAlignCenter, &cads_font12, score_text, CadsColorGrayDark);

    if(g->dodger.game_over) {
        cads_rect_t overlay = {area.x, (int16_t)(area.y + area.height / 2 - 20), area.width, 40};
        cads_canvas_fill_rect(overlay.x, overlay.y, overlay.width, overlay.height, CadsColorWhite);
        cads_canvas_draw_text_aligned(overlay, CadsAlignCenter, &cads_font16, "Game Over - OK to retry", CadsColorRed);
    }
}

bool cads_game_dodger_input(const cads_input_event_t* event, cads_game_dodger_t* g, uint32_t now_ms) {
    if(g->dodger.game_over && event->type == CadsInputRelease && event->key == CadsKeyOk) {
        cads_game_dodger_start(g, g->dodger.field_width, g->dodger.field_height, now_ms);
        return true;
    }
    return false; /* Up/Down are polled in the tick, not consumed as events here */
}
