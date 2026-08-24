/*
 * CaDS Zero - Snake, one cartridge in Leo's Arcade.
 *
 * The rules live in cads/toolbox/snake.h and are unit-tested there
 * (tests/unit/test_snake.c) with hand-built board states; this file is
 * only the grid-to-pixel mapping, score/game-over overlay, and turning
 * Up/Down/Left/Right key presses into cads_snake_set_direction() calls.
 *
 * A 20px header strip is reserved above the grid for the score, so the
 * grid itself uses (area.height - 20) rather than the full view height -
 * the same reason cads_game_breakout.c's playfield does not draw its
 * score over the ball.
 */

#include "cads_game_internal.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"

#define CADS_GAME_SNAKE_HEADER_HEIGHT 20

static void cads_game_snake_start(cads_game_snake_t* g, uint32_t now_ms) {
    cads_snake_init(&g->snake, g->grid_width, g->grid_height, g->rng_state);
    g->last_step_ms = now_ms;
}

void cads_game_snake_reset(cads_game_snake_t* g, cads_rect_t area, uint32_t now_ms) {
    g->area = area;
    int16_t grid_area_height = (int16_t)(area.height - CADS_GAME_SNAKE_HEADER_HEIGHT);
    int16_t width_cells = (int16_t)(area.width / CADS_GAME_SNAKE_CELL);
    int16_t height_cells = (int16_t)(grid_area_height / CADS_GAME_SNAKE_CELL);
    if(width_cells < 4) width_cells = 4;
    if(height_cells < 4) height_cells = 4;
    g->grid_width = (uint8_t)width_cells;
    g->grid_height = (uint8_t)height_cells;

    g->rng_state = (uint32_t)cads_hal_ticks_us();
    if(g->rng_state == 0u) g->rng_state = 0x9E3779B9u;

    cads_game_snake_start(g, now_ms);
}

static uint32_t cads_game_snake_rand(cads_game_snake_t* g) {
    uint32_t x = g->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng_state = x;
    return x;
}

bool cads_game_snake_tick(cads_game_snake_t* g, cads_rect_t area, uint32_t now_ms) {
    (void)area;
    if(g->snake.game_over) return false;
    if(now_ms - g->last_step_ms < CADS_GAME_SNAKE_STEP_MS) return false;

    cads_snake_step(&g->snake, cads_game_snake_rand(g));
    g->last_step_ms = now_ms;
    return true;
}

void cads_game_snake_draw(cads_rect_t area, const cads_game_snake_t* g) {
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorWhite);

    char score_text[24];
    size_t pos = cads_str_copy(score_text, sizeof(score_text), "Score: ");
    cads_fmt_uint(score_text + pos, sizeof(score_text) - pos, g->snake.score);
    cads_rect_t header = {area.x, area.y, area.width, CADS_GAME_SNAKE_HEADER_HEIGHT};
    cads_canvas_draw_text_aligned(header, CadsAlignCenter, &cads_font16, score_text, CadsColorGrayDark);

    int16_t grid_y = (int16_t)(area.y + CADS_GAME_SNAKE_HEADER_HEIGHT);

    cads_canvas_fill_rect(
        (int16_t)(area.x + g->snake.food_x * CADS_GAME_SNAKE_CELL),
        (int16_t)(grid_y + g->snake.food_y * CADS_GAME_SNAKE_CELL),
        CADS_GAME_SNAKE_CELL - 1, CADS_GAME_SNAKE_CELL - 1, CadsColorRed);

    for(uint8_t i = 0u; i < g->snake.length; i++) {
        cads_color_t color = (i == 0u) ? CadsColorBrand : CadsColorAccent;
        cads_canvas_fill_rect(
            (int16_t)(area.x + g->snake.body_x[i] * CADS_GAME_SNAKE_CELL),
            (int16_t)(grid_y + g->snake.body_y[i] * CADS_GAME_SNAKE_CELL),
            CADS_GAME_SNAKE_CELL - 1, CADS_GAME_SNAKE_CELL - 1, color);
    }

    if(g->snake.game_over) {
        cads_rect_t overlay = {area.x, (int16_t)(grid_y + (area.height - CADS_GAME_SNAKE_HEADER_HEIGHT) / 2 - 20),
                                area.width, 40};
        cads_canvas_fill_rect(overlay.x, overlay.y, overlay.width, overlay.height, CadsColorWhite);
        cads_canvas_draw_text_aligned(overlay, CadsAlignCenter, &cads_font16, "Game Over - OK to retry", CadsColorRed);
    }
}

bool cads_game_snake_input(const cads_input_event_t* event, cads_game_snake_t* g, uint32_t now_ms) {
    if(event->type != CadsInputPress && event->type != CadsInputRelease) return false;

    if(g->snake.game_over) {
        if(event->type == CadsInputRelease && event->key == CadsKeyOk) {
            cads_game_snake_start(g, now_ms);
            return true;
        }
        return false;
    }

    if(event->type != CadsInputPress) return false;

    switch(event->key) {
        case CadsKeyUp: cads_snake_set_direction(&g->snake, CadsSnakeUp); return true;
        case CadsKeyDown: cads_snake_set_direction(&g->snake, CadsSnakeDown); return true;
        case CadsKeyLeft: cads_snake_set_direction(&g->snake, CadsSnakeLeft); return true;
        case CadsKeyRight: cads_snake_set_direction(&g->snake, CadsSnakeRight); return true;
        default: return false;
    }
}
