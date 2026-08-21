/*
 * CaDS Zero - Leo's Reflex Test.
 *
 * docs/ROADMAP.md's own wording for this bullet: "a game, to exercise the
 * input and timing paths end to end". A reaction-time test is a literal
 * reading of that rather than a loose one - the whole point of the game
 * IS measuring the time between a stimulus and an input event, using the
 * same cads_hal_ticks_ms() this session's own hardware verification has
 * relied on throughout (the MMC-counter cross-checks, the pktgen pacing,
 * the sniffer's capture window).
 *
 * WHY A TICK, NOT JUST AN INPUT HANDLER
 * -----------------------------------------
 * cads_view_t (cads_view.h) is draw/input/enter/exit and nothing else - a
 * view never wakes up on its own. That is fine for every other app in
 * this tree (a menu row only changes because a key moved it), but this
 * game's "wait for it..." phase has to end on ITS OWN, with no key
 * pressed at all - that is the entire test. cads_desktop.c already hit
 * this exact problem for Leo's blink and solved it the same way:
 * cads_game_tick(), called once per main loop iteration alongside
 * cads_desktop_tick()/cads_gpio_tick(), the same pattern, not a new one.
 *
 * A LOCAL PRNG, NOT A NEW TOOLBOX MODULE
 * -------------------------------------------
 * Nothing else in this codebase has needed randomness yet, so there is no
 * cads/toolbox/rng.h to reach for. A 32-bit xorshift, seeded once from
 * cads_hal_ticks_us() (which is running free-of-any-input the instant
 * cads_game_init() is called, so its low bits are as good a seed as this
 * firmware has), is the standard minimal choice and lives here rather
 * than in the toolbox - promote it if a second caller ever needs one.
 */

#include "cads_game.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

typedef enum {
    CADS_GAME_STATE_IDLE = 0, /**< "press OK to start"                       */
    CADS_GAME_STATE_ARMED,    /**< waiting out the random delay              */
    CADS_GAME_STATE_GO,       /**< signal is up, waiting for the OK press    */
    CADS_GAME_STATE_TOO_SOON, /**< OK arrived during ARMED - a false start   */
    CADS_GAME_STATE_RESULT,   /**< showing this round's and the best time    */
} cads_game_state_t;

/* A real reflex test varies the wait so the player cannot anticipate it
 * by counting; 800..2800 ms is long enough that a false start is a real
 * mistake, short enough that a round does not feel like a delay. */
#define CADS_GAME_ARMED_MIN_MS 800u
#define CADS_GAME_ARMED_SPAN_MS 2000u

typedef struct {
    cads_view_t view;
    cads_game_state_t state;
    uint32_t state_entered_ms;
    uint32_t armed_delay_ms;
    uint32_t reaction_ms;
    uint32_t best_ms; /**< 0 means "no round finished cleanly yet" */
    uint32_t rounds_played;
    uint32_t rng_state;
} cads_game_t;

static cads_game_t s_game;
static bool s_game_ready;

static uint32_t cads_game_rand(cads_game_t* app) {
    /* xorshift32 - Marsaglia's constants; passes far more scrutiny than
     * this one-in-2000-ms decision actually needs. */
    uint32_t x = app->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    app->rng_state = x;
    return x;
}

static void cads_game_enter_state(cads_game_t* app, cads_game_state_t state, uint32_t now_ms) {
    app->state = state;
    app->state_entered_ms = now_ms;
    cads_view_dirty(&app->view);
}

static void cads_game_start_round(cads_game_t* app, uint32_t now_ms) {
    app->armed_delay_ms = CADS_GAME_ARMED_MIN_MS + (cads_game_rand(app) % CADS_GAME_ARMED_SPAN_MS);
    cads_game_enter_state(app, CADS_GAME_STATE_ARMED, now_ms);
}

void cads_game_tick(uint32_t now_ms) {
    if(!s_game_ready) return;
    cads_game_t* app = &s_game;

    if(app->state == CADS_GAME_STATE_ARMED &&
       now_ms - app->state_entered_ms >= app->armed_delay_ms) {
        cads_game_enter_state(app, CADS_GAME_STATE_GO, now_ms);
    }
}

static void cads_game_draw(cads_rect_t area, void* context) {
    cads_game_t* app = (cads_game_t*)context;

    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorWhite);

    const char* line1 = "";
    const char* line2 = "";
    cads_color_t color1 = CadsColorBrand;
    char reaction_text[48];

    switch(app->state) {
        case CADS_GAME_STATE_IDLE:
            line1 = "Leo's Reflex Test";
            line2 = "Press OK to start";
            break;
        case CADS_GAME_STATE_ARMED:
            color1 = CadsColorGrayDark;
            line1 = "Wait for it...";
            break;
        case CADS_GAME_STATE_GO:
            color1 = CadsColorAccent;
            line1 = "GO!";
            line2 = "Press OK now";
            break;
        case CADS_GAME_STATE_TOO_SOON:
            color1 = CadsColorRed;
            line1 = "Too soon!";
            line2 = "Press OK to try again";
            break;
        case CADS_GAME_STATE_RESULT: {
            color1 = CadsColorBrand;
            size_t pos = cads_str_append(reaction_text, sizeof(reaction_text), "Reaction: ");
            pos += cads_fmt_uint(reaction_text + pos, sizeof(reaction_text) - pos, app->reaction_ms);
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

    if(app->best_ms != 0u && app->state != CADS_GAME_STATE_ARMED &&
       app->state != CADS_GAME_STATE_GO) {
        char best_text[32];
        size_t pos = cads_str_append(best_text, sizeof(best_text), "Best: ");
        pos += cads_fmt_uint(best_text + pos, sizeof(best_text) - pos, app->best_ms);
        cads_str_append(best_text + pos, sizeof(best_text) - pos, " ms");
        cads_rect_t footer = {
            area.x, area.y + area.height - 24, area.width, 24};
        cads_canvas_draw_text_aligned(footer, CadsAlignCenter, &cads_font12, best_text, CadsColorGray);
    }
}

static bool cads_game_input(const cads_input_event_t* event, void* context) {
    cads_game_t* app = (cads_game_t*)context;

    if(event->type != CadsInputRelease || event->key != CadsKeyOk) return false;

    switch(app->state) {
        case CADS_GAME_STATE_IDLE:
        case CADS_GAME_STATE_TOO_SOON:
        case CADS_GAME_STATE_RESULT:
            cads_game_start_round(app, event->timestamp);
            return true;
        case CADS_GAME_STATE_ARMED:
            /* The reaction test's own failure mode: pressed before the
             * signal, not after it. */
            cads_game_enter_state(app, CADS_GAME_STATE_TOO_SOON, event->timestamp);
            return true;
        case CADS_GAME_STATE_GO:
            app->reaction_ms = event->timestamp - app->state_entered_ms;
            if(app->best_ms == 0u || app->reaction_ms < app->best_ms) {
                app->best_ms = app->reaction_ms;
            }
            app->rounds_played++;
            cads_game_enter_state(app, CADS_GAME_STATE_RESULT, event->timestamp);
            return true;
    }
    return false;
}

static void cads_game_enter(void* context) {
    cads_game_t* app = (cads_game_t*)context;
    app->state = CADS_GAME_STATE_IDLE;
    app->state_entered_ms = cads_hal_ticks_ms();
    cads_view_dirty(&app->view);
}

static const cads_softkey_t cads_game_keys[] = {
    {CadsKeyOk, "Go"},
    {CadsKeyBack, "Back"},
};

void cads_game_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_game.state = CADS_GAME_STATE_IDLE;
    s_game.state_entered_ms = cads_hal_ticks_ms();
    s_game.armed_delay_ms = 0u;
    s_game.reaction_ms = 0u;
    s_game.best_ms = 0u;
    s_game.rounds_played = 0u;
    /* xorshift32 never recovers from a zero state - the low bits of the
     * free-running microsecond clock are effectively never exactly zero
     * at boot, but this guards the theoretical case rather than trusting
     * it. */
    s_game.rng_state = (uint32_t)cads_hal_ticks_us();
    if(s_game.rng_state == 0u) s_game.rng_state = 0x9E3779B9u;

    cads_view_init(&s_game.view, cads_game_draw, cads_game_input, &s_game);
    cads_view_set_lifecycle(&s_game.view, cads_game_enter, NULL);
    cads_view_set_title(&s_game.view, "Reflex Test");
    cads_view_set_softkeys(
        &s_game.view, cads_game_keys, sizeof(cads_game_keys) / sizeof(cads_game_keys[0]));

    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_GAME, &s_game.view);
    s_game_ready = true;
}
