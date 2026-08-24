/*
 * CaDS Zero - Breakout, one cartridge in Leo's Arcade.
 *
 * The ball/paddle/brick physics live in cads/toolbox/breakout.h and are
 * unit-tested there (tests/unit/test_breakout.c); this file is only the
 * pixel-space setup, score/lives/game-over overlay, and turning
 * Left/Right into paddle movement.
 *
 * WHY THE PADDLE IS POLLED, NOT EVENT DRIVEN
 * -------------------------------------------------------------------
 * Snake's direction only changes on a Press edge - one key, one turn. A
 * paddle wants to keep moving for as long as the key stays down, which is
 * exactly what cads_input_is_down() reports and a Press/Repeat event pair
 * would only approximate. Polling it once per physics step
 * (CADS_GAME_BREAKOUT_STEP_MS, not once per cads_game_tick() call - see
 * that constant's own comment in cads_game_internal.h) keeps paddle speed
 * independent of how often the main loop happens to call tick.
 */

#include "cads_game_internal.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"

static void cads_game_breakout_start(cads_game_breakout_t* g, int16_t field_width, int16_t field_height, uint32_t now_ms) {
    cads_breakout_init(&g->breakout, field_width, field_height);
    g->last_step_ms = now_ms;
}

void cads_game_breakout_reset(cads_game_breakout_t* g, cads_rect_t area, uint32_t now_ms) {
    cads_game_breakout_start(g, area.width, area.height, now_ms);
}

bool cads_game_breakout_tick(cads_game_breakout_t* g, cads_rect_t area, uint32_t now_ms) {
    (void)area;
    if(g->breakout.game_over) return false;
    if(now_ms - g->last_step_ms < CADS_GAME_BREAKOUT_STEP_MS) return false;
    g->last_step_ms = now_ms;

    int16_t paddle_x = g->breakout.paddle_x;
    if(cads_input_is_down(CadsKeyLeft)) paddle_x = (int16_t)(paddle_x - CADS_GAME_BREAKOUT_PADDLE_STEP);
    if(cads_input_is_down(CadsKeyRight)) paddle_x = (int16_t)(paddle_x + CADS_GAME_BREAKOUT_PADDLE_STEP);
    if(paddle_x != g->breakout.paddle_x) cads_breakout_set_paddle_x(&g->breakout, paddle_x);

    cads_breakout_step(&g->breakout);
    return true;
}

void cads_game_breakout_draw(cads_rect_t area, const cads_game_breakout_t* g) {
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorWhite);

    for(size_t row = 0u; row < CADS_BREAKOUT_ROWS; row++) {
        for(size_t col = 0u; col < CADS_BREAKOUT_COLS; col++) {
            if(!g->breakout.bricks[row * CADS_BREAKOUT_COLS + col]) continue;
            int16_t cell_w = (int16_t)(g->breakout.field_width / CADS_BREAKOUT_COLS);
            cads_canvas_fill_rect(
                (int16_t)(area.x + (int16_t)col * cell_w),
                (int16_t)(area.y + (int16_t)row * CADS_BREAKOUT_BRICK_ROW_HEIGHT),
                (int16_t)(cell_w - 1), CADS_BREAKOUT_BRICK_ROW_HEIGHT - 1,
                (row == 0u) ? CadsColorRed : ((row == 1u) ? CadsColorAmber : CadsColorAccent));
        }
    }

    cads_canvas_fill_rect(
        (int16_t)(area.x + g->breakout.paddle_x), (int16_t)(area.y + g->breakout.field_height - CADS_BREAKOUT_PADDLE_HEIGHT),
        g->breakout.paddle_width, CADS_BREAKOUT_PADDLE_HEIGHT, CadsColorBrand);

    cads_canvas_fill_rect(
        (int16_t)(area.x + g->breakout.ball_x - 2), (int16_t)(area.y + g->breakout.ball_y - 2), 5, 5,
        CadsColorGrayDark);

    char status_text[40];
    size_t pos = cads_str_copy(status_text, sizeof(status_text), "Score: ");
    pos += cads_fmt_uint(status_text + pos, sizeof(status_text) - pos, g->breakout.score);
    pos += cads_str_append(status_text + pos, sizeof(status_text) - pos, "  Lives: ");
    cads_fmt_uint(status_text + pos, sizeof(status_text) - pos, g->breakout.lives);
    cads_rect_t footer = {area.x, (int16_t)(area.y + area.height - 18), area.width, 18};
    cads_canvas_draw_text_aligned(footer, CadsAlignCenter, &cads_font12, status_text, CadsColorGrayDark);

    if(g->breakout.game_over) {
        const char* message = g->breakout.cleared ? "Cleared! OK to play again" : "Game Over - OK to retry";
        cads_rect_t overlay = {area.x, (int16_t)(area.y + area.height / 2 - 20), area.width, 40};
        cads_canvas_fill_rect(overlay.x, overlay.y, overlay.width, overlay.height, CadsColorWhite);
        cads_canvas_draw_text_aligned(
            overlay, CadsAlignCenter, &cads_font16, message, g->breakout.cleared ? CadsColorAccent : CadsColorRed);
    }
}

bool cads_game_breakout_input(const cads_input_event_t* event, cads_game_breakout_t* g, uint32_t now_ms) {
    if(g->breakout.game_over && event->type == CadsInputRelease && event->key == CadsKeyOk) {
        cads_game_breakout_start(g, g->breakout.field_width, g->breakout.field_height, now_ms);
        return true;
    }
    return false; /* Left/Right are polled in the tick, not consumed as events here */
}
