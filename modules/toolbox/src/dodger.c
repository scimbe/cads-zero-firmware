#include "cads/toolbox/dodger.h"

/* Three independent gap heights out of one caller-supplied rng draw,
 * the same "mix, don't re-roll" approach cads_snake_place_food() uses -
 * this file has no RNG of its own, only what it is handed. */
static int16_t cads_dodger_random_gap_y(int16_t field_height, uint32_t rng, uint32_t salt) {
    uint32_t mixed = rng + salt * 0x9E3779B9u;
    int16_t max_top = (int16_t)(field_height - CADS_DODGER_GAP_HEIGHT);
    if(max_top < 1) max_top = 1;
    return (int16_t)(mixed % (uint32_t)max_top);
}

void cads_dodger_init(cads_dodger_t* d, int16_t field_width, int16_t field_height, uint32_t rng) {
    d->field_width = field_width;
    d->field_height = field_height;
    d->player_x = (int16_t)(field_width / 5);
    d->player_y = (int16_t)((field_height - CADS_DODGER_PLAYER_SIZE) / 2);
    d->score = 0u;
    d->game_over = false;

    int16_t spacing = (int16_t)(field_width / 2);
    if(spacing < CADS_DODGER_OBSTACLE_WIDTH * 3) spacing = CADS_DODGER_OBSTACLE_WIDTH * 3;
    for(uint32_t i = 0u; i < CADS_DODGER_OBSTACLE_COUNT; i++) {
        d->obstacle_x[i] = (int16_t)(field_width + (int16_t)i * spacing);
        d->obstacle_gap_y[i] = cads_dodger_random_gap_y(field_height, rng, i + 1u);
        d->obstacle_scored[i] = false;
    }
}

void cads_dodger_move(cads_dodger_t* d, int16_t dy) {
    int16_t y = (int16_t)(d->player_y + dy);
    if(y < 0) y = 0;
    if(y > d->field_height - CADS_DODGER_PLAYER_SIZE) y = (int16_t)(d->field_height - CADS_DODGER_PLAYER_SIZE);
    d->player_y = y;
}

void cads_dodger_step(cads_dodger_t* d, uint32_t rng) {
    if(d->game_over) return;

    for(uint32_t i = 0u; i < CADS_DODGER_OBSTACLE_COUNT; i++) {
        d->obstacle_x[i] = (int16_t)(d->obstacle_x[i] - CADS_DODGER_SPEED);

        int16_t obstacle_right = (int16_t)(d->obstacle_x[i] + CADS_DODGER_OBSTACLE_WIDTH);
        int16_t player_right = (int16_t)(d->player_x + CADS_DODGER_PLAYER_SIZE);
        bool x_overlap = d->obstacle_x[i] < player_right && obstacle_right > d->player_x;

        if(x_overlap) {
            int16_t gap_top = d->obstacle_gap_y[i];
            int16_t gap_bottom = (int16_t)(gap_top + CADS_DODGER_GAP_HEIGHT);
            int16_t player_bottom = (int16_t)(d->player_y + CADS_DODGER_PLAYER_SIZE);
            if(d->player_y < gap_top || player_bottom > gap_bottom) {
                d->game_over = true;
                return;
            }
        } else if(obstacle_right < d->player_x && !d->obstacle_scored[i]) {
            d->score++;
            d->obstacle_scored[i] = true;
        }

        if(obstacle_right < 0) {
            d->obstacle_x[i] = d->field_width;
            d->obstacle_gap_y[i] = cads_dodger_random_gap_y(d->field_height, rng, i + 1u);
            d->obstacle_scored[i] = false;
        }
    }
}
