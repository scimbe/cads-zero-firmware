#include "cads/toolbox/breakout.h"

#include <stddef.h>

static void cads_breakout_serve(cads_breakout_t* b) {
    b->ball_x = (int16_t)(b->field_width / 2);
    b->ball_y = (int16_t)(b->field_height - CADS_BREAKOUT_PADDLE_HEIGHT - 20);
    b->vel_x = 2;
    b->vel_y = -2;
}

void cads_breakout_init(cads_breakout_t* b, int16_t field_width, int16_t field_height) {
    b->field_width = field_width;
    b->field_height = field_height;
    b->paddle_width = (int16_t)(field_width / 6);
    b->paddle_x = (int16_t)((field_width - b->paddle_width) / 2);

    for(size_t i = 0u; i < CADS_BREAKOUT_BRICK_COUNT; i++) b->bricks[i] = true;

    b->score = 0u;
    b->lives = CADS_BREAKOUT_START_LIVES;
    b->game_over = false;
    b->cleared = false;
    cads_breakout_serve(b);
}

void cads_breakout_set_paddle_x(cads_breakout_t* b, int16_t x) {
    if(x < 0) x = 0;
    if(x > b->field_width - b->paddle_width) x = (int16_t)(b->field_width - b->paddle_width);
    b->paddle_x = x;
}

void cads_breakout_step(cads_breakout_t* b) {
    if(b->game_over) return;

    b->ball_x = (int16_t)(b->ball_x + b->vel_x);
    b->ball_y = (int16_t)(b->ball_y + b->vel_y);

    if(b->ball_x <= 0) {
        b->ball_x = 0;
        b->vel_x = (int16_t)(-b->vel_x);
    } else if(b->ball_x >= b->field_width - 1) {
        b->ball_x = (int16_t)(b->field_width - 1);
        b->vel_x = (int16_t)(-b->vel_x);
    }

    int16_t brick_area_height = (int16_t)(CADS_BREAKOUT_ROWS * CADS_BREAKOUT_BRICK_ROW_HEIGHT);
    if(b->vel_y < 0 && b->ball_y < brick_area_height && b->ball_y >= 0) {
        int16_t col = (int16_t)(b->ball_x * (int16_t)CADS_BREAKOUT_COLS / b->field_width);
        int16_t row = (int16_t)(b->ball_y / CADS_BREAKOUT_BRICK_ROW_HEIGHT);
        if(col < 0) col = 0;
        if(col >= (int16_t)CADS_BREAKOUT_COLS) col = (int16_t)CADS_BREAKOUT_COLS - 1;
        if(row < 0) row = 0;
        if(row >= (int16_t)CADS_BREAKOUT_ROWS) row = (int16_t)CADS_BREAKOUT_ROWS - 1;

        size_t idx = (size_t)row * CADS_BREAKOUT_COLS + (size_t)col;
        if(b->bricks[idx]) {
            b->bricks[idx] = false;
            b->score++;
            b->vel_y = (int16_t)(-b->vel_y);

            bool any_left = false;
            for(size_t i = 0u; i < CADS_BREAKOUT_BRICK_COUNT; i++) {
                if(b->bricks[i]) {
                    any_left = true;
                    break;
                }
            }
            if(!any_left) {
                b->cleared = true;
                b->game_over = true;
                return;
            }
        }
    }
    if(b->ball_y <= 0) {
        b->ball_y = 0;
        b->vel_y = (int16_t)(-b->vel_y);
    }

    int16_t paddle_y = (int16_t)(b->field_height - CADS_BREAKOUT_PADDLE_HEIGHT);
    if(b->vel_y > 0 && b->ball_y >= paddle_y && b->ball_x >= b->paddle_x &&
       b->ball_x <= b->paddle_x + b->paddle_width) {
        b->ball_y = paddle_y;
        b->vel_y = (int16_t)(-b->vel_y);
        return;
    }

    if(b->ball_y > b->field_height) {
        if(b->lives > 0u) b->lives--;
        if(b->lives == 0u) {
            b->game_over = true;
            return;
        }
        cads_breakout_serve(b);
    }
}
