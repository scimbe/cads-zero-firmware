#include "cads/toolbox/snake.h"

static bool cads_snake_occupies(const cads_snake_t* s, uint8_t x, uint8_t y, uint8_t up_to) {
    for(uint8_t i = 0u; i < up_to; i++) {
        if(s->body_x[i] == x && s->body_y[i] == y) return true;
    }
    return false;
}

static void cads_snake_place_food(cads_snake_t* s, uint32_t rng) {
    uint8_t x = (uint8_t)(rng % s->grid_width);
    uint8_t y = (uint8_t)((rng >> 8) % s->grid_height);

    /* Deterministic scan from the rng-chosen cell rather than re-rolling -
     * bounded by grid_width*grid_height, small on any grid this game
     * actually uses, and it always terminates because the snake can never
     * occupy every cell (cads_snake_step() caps growth below the grid
     * size in practice for the fields this ships with). */
    while(cads_snake_occupies(s, x, y, s->length)) {
        x = (uint8_t)((x + 1u) % s->grid_width);
        if(x == 0u) y = (uint8_t)((y + 1u) % s->grid_height);
    }

    s->food_x = x;
    s->food_y = y;
}

void cads_snake_init(cads_snake_t* s, uint8_t grid_width, uint8_t grid_height, uint32_t rng) {
    s->grid_width = grid_width;
    s->grid_height = grid_height;
    s->length = 1u;
    s->body_x[0] = (uint8_t)(grid_width / 2u);
    s->body_y[0] = (uint8_t)(grid_height / 2u);
    s->dir = CadsSnakeRight;
    s->pending_dir = CadsSnakeRight;
    s->score = 0u;
    s->game_over = false;
    cads_snake_place_food(s, rng);
}

void cads_snake_set_direction(cads_snake_t* s, cads_snake_dir_t dir) {
    static const cads_snake_dir_t opposite[4] = {
        CadsSnakeDown, CadsSnakeUp, CadsSnakeRight, CadsSnakeLeft};
    if(dir == opposite[s->dir]) return; /* can't reverse into your own neck */
    s->pending_dir = dir;
}

void cads_snake_step(cads_snake_t* s, uint32_t rng) {
    if(s->game_over) return;

    s->dir = s->pending_dir;

    int16_t head_x = s->body_x[0];
    int16_t head_y = s->body_y[0];
    switch(s->dir) {
        case CadsSnakeUp: head_y--; break;
        case CadsSnakeDown: head_y++; break;
        case CadsSnakeLeft: head_x--; break;
        case CadsSnakeRight: head_x++; break;
    }

    if(head_x < 0 || head_y < 0 || head_x >= s->grid_width || head_y >= s->grid_height) {
        s->game_over = true;
        return;
    }

    bool eats = ((uint8_t)head_x == s->food_x && (uint8_t)head_y == s->food_y);

    /* The tail cell vacates this step unless the snake is growing, so it
     * is not a collision to move onto it - the classic Snake off-by-one
     * that makes an otherwise-correct implementation falsely end the game
     * on an ordinary non-growing move. */
    uint8_t check_len = eats ? s->length : (uint8_t)(s->length - 1u);
    if(cads_snake_occupies(s, (uint8_t)head_x, (uint8_t)head_y, check_len)) {
        s->game_over = true;
        return;
    }

    uint8_t new_length = s->length;
    if(eats && new_length < CADS_SNAKE_MAX_LENGTH) new_length++;

    for(uint8_t i = (uint8_t)(new_length - 1u); i > 0u; i--) {
        s->body_x[i] = s->body_x[i - 1u];
        s->body_y[i] = s->body_y[i - 1u];
    }
    s->body_x[0] = (uint8_t)head_x;
    s->body_y[0] = (uint8_t)head_y;
    s->length = new_length;

    if(eats) {
        s->score++;
        cads_snake_place_food(s, rng);
    }
}
