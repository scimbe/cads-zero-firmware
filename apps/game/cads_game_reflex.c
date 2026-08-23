/*
 * CaDS Zero - Leo's Reflex Test, one cartridge in Leo's Arcade.
 *
 * Unchanged behaviour from the original single-game apps/game/cads_game.c
 * (docs/ROADMAP.md's own wording for the original bullet: "a game, to
 * exercise the input and timing paths end to end" - a reaction-time test
 * is a literal reading of that). Only the packaging moved: this file now
 * owns just the reflex state and its own draw/input/tick, dispatched by
 * cads_game.c's select screen instead of being the whole app.
 *
 * A LOCAL PRNG, NOT A SHARED ONE
 * -------------------------------------------------------------------
 * Each of the four cartridges seeds its own xorshift32 from
 * cads_hal_ticks_us() at the moment it is actually selected, rather than
 * the hub owning one shared generator - four independent games do not
 * need to agree on a draw order, and a shared generator would make it one
 * more than any one of them needs.
 */

#include "cads_game_internal.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"

#define CADS_GAME_REFLEX_ARMED_MIN_MS 800u
#define CADS_GAME_REFLEX_ARMED_SPAN_MS 2000u

static uint32_t cads_game_reflex_rand(cads_game_reflex_t* r) {
    uint32_t x = r->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->rng_state = x;
    return x;
}

static void cads_game_reflex_enter_state(
    cads_game_reflex_t* r, cads_game_reflex_state_t state, uint32_t now_ms) {
    r->state = state;
    r->state_entered_ms = now_ms;
}

static void cads_game_reflex_start_round(cads_game_reflex_t* r, uint32_t now_ms) {
    r->armed_delay_ms =
        CADS_GAME_REFLEX_ARMED_MIN_MS + (cads_game_reflex_rand(r) % CADS_GAME_REFLEX_ARMED_SPAN_MS);
    cads_game_reflex_enter_state(r, CADS_GAME_REFLEX_ARMED, now_ms);
}

void cads_game_reflex_reset(cads_game_reflex_t* r, uint32_t now_ms) {
    r->state = CADS_GAME_REFLEX_IDLE;
    r->state_entered_ms = now_ms;
    r->armed_delay_ms = 0u;
    r->reaction_ms = 0u;
    r->best_ms = 0u;
    r->rounds_played = 0u;
    r->rng_state = (uint32_t)cads_hal_ticks_us();
    if(r->rng_state == 0u) r->rng_state = 0x9E3779B9u;
}

bool cads_game_reflex_tick(cads_game_reflex_t* r, uint32_t now_ms) {
    if(r->state == CADS_GAME_REFLEX_ARMED && now_ms - r->state_entered_ms >= r->armed_delay_ms) {
        cads_game_reflex_enter_state(r, CADS_GAME_REFLEX_GO, now_ms);
        return true;
    }
    return false;
}

void cads_game_reflex_draw(cads_rect_t area, const cads_game_reflex_t* r) {
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorWhite);

    const char* line1 = "";
    const char* line2 = "";
    cads_color_t color1 = CadsColorBrand;
    char reaction_text[48];

    switch(r->state) {
        case CADS_GAME_REFLEX_IDLE:
            line1 = "Leo's Reflex Test";
            line2 = "Press OK to start";
            break;
        case CADS_GAME_REFLEX_ARMED:
            color1 = CadsColorGrayDark;
            line1 = "Wait for it...";
            break;
        case CADS_GAME_REFLEX_GO:
            color1 = CadsColorAccent;
            line1 = "GO!";
            line2 = "Press OK now";
            break;
        case CADS_GAME_REFLEX_TOO_SOON:
            color1 = CadsColorRed;
            line1 = "Too soon!";
            line2 = "Press OK to try again";
            break;
        case CADS_GAME_REFLEX_RESULT: {
            color1 = CadsColorBrand;
            size_t pos = cads_str_append(reaction_text, sizeof(reaction_text), "Reaction: ");
            pos += cads_fmt_uint(reaction_text + pos, sizeof(reaction_text) - pos, r->reaction_ms);
            cads_str_append(reaction_text + pos, sizeof(reaction_text) - pos, " ms");
            line1 = reaction_text;
            line2 = "Press OK to try again";
            break;
        }
    }

    cads_rect_t upper = {area.x, area.y, area.width, area.height / 2};
    cads_rect_t lower = {area.x, area.y + area.height / 2, area.width, area.height / 2};
    cads_canvas_draw_text_aligned(upper, CadsAlignCenter, &cads_font24, line1, color1);
    if(line2[0] != '\0') {
        cads_canvas_draw_text_aligned(lower, CadsAlignCenter, &cads_font16, line2, CadsColorGrayDark);
    }

    if(r->best_ms != 0u && r->state != CADS_GAME_REFLEX_ARMED && r->state != CADS_GAME_REFLEX_GO) {
        char best_text[32];
        size_t pos = cads_str_append(best_text, sizeof(best_text), "Best: ");
        pos += cads_fmt_uint(best_text + pos, sizeof(best_text) - pos, r->best_ms);
        cads_str_append(best_text + pos, sizeof(best_text) - pos, " ms");
        cads_rect_t footer = {area.x, area.y + area.height - 24, area.width, 24};
        cads_canvas_draw_text_aligned(footer, CadsAlignCenter, &cads_font12, best_text, CadsColorGray);
    }
}

bool cads_game_reflex_input(const cads_input_event_t* event, cads_game_reflex_t* r) {
    if(event->type != CadsInputRelease || event->key != CadsKeyOk) return false;

    switch(r->state) {
        case CADS_GAME_REFLEX_IDLE:
        case CADS_GAME_REFLEX_TOO_SOON:
        case CADS_GAME_REFLEX_RESULT:
            cads_game_reflex_start_round(r, event->timestamp);
            return true;
        case CADS_GAME_REFLEX_ARMED:
            cads_game_reflex_enter_state(r, CADS_GAME_REFLEX_TOO_SOON, event->timestamp);
            return true;
        case CADS_GAME_REFLEX_GO:
            r->reaction_ms = event->timestamp - r->state_entered_ms;
            if(r->best_ms == 0u || r->reaction_ms < r->best_ms) r->best_ms = r->reaction_ms;
            r->rounds_played++;
            cads_game_reflex_enter_state(r, CADS_GAME_REFLEX_RESULT, event->timestamp);
            return true;
    }
    return false;
}
