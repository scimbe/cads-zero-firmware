/*
 * CaDS Zero - Leo's Arcade: a select screen over four cartridges.
 *
 * WHY FOUR REAL VIEWS, NOT ONE VIEW WITH AN INTERNAL "MODE"
 * -------------------------------------------------------------------
 * apps/settings/README.md already documents the constraint this design
 * works around: "the soft-key strip only gets re-applied by the
 * compositor when the current view changes... a view has no supported
 * way to update its own key labels while it stays current." Snake wants
 * Up/Down/Left/Right, Breakout wants Left/Right, Dodger wants Up/Down,
 * and the select screen wants Up/Down/Play - four different strips, so
 * this is four real views, each cads_view_dispatcher_push()ed from the
 * select screen and each carrying its own softkeys, the same fix
 * apps/settings already used for its confirm dialog. It also means Back
 * needs no special handling anywhere in this file: a cartridge that does
 * not consume it falls through to the dispatcher's own default pop,
 * landing back on the select screen exactly the way any other pushed
 * view returns to whatever pushed it.
 *
 * WHY cads_game_tick() CHECKS THE CURRENT VIEW ID FIRST
 * -------------------------------------------------------------------
 * The original single-game version of this file ticked unconditionally,
 * which was harmless with only one game to tick. With four, ticking all
 * of them regardless of which is on screen would let Snake starve while
 * the player is looking at Breakout - a real gameplay bug, not just
 * wasted cycles - so this checks cads_view_dispatcher_current_id() and
 * only advances the one actually showing.
 */

#include "cads_game.h"

#include <stddef.h>

#include "cads_game_internal.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_view.h"

#define CADS_GAME_CARTRIDGE_COUNT 4u
#define CADS_GAME_SELECT_ROW_HEIGHT 32

typedef struct {
    const char* name;
    uint32_t view_id;
} cads_game_cartridge_t;

static const cads_game_cartridge_t cads_game_cartridges[CADS_GAME_CARTRIDGE_COUNT] = {
    {"Reflex Test", CADS_VIEW_ID_GAME_REFLEX},
    {"Snake", CADS_VIEW_ID_GAME_SNAKE},
    {"Breakout", CADS_VIEW_ID_GAME_BREAKOUT},
    {"Dodger", CADS_VIEW_ID_GAME_DODGER},
};

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    uint32_t selected;
} cads_game_select_t;

static cads_game_select_t s_select;
static bool s_game_ready;

static cads_view_t s_reflex_view;
static cads_game_reflex_t s_reflex;

static cads_view_t s_snake_view;
static cads_game_snake_t s_snake;

static cads_view_t s_breakout_view;
static cads_game_breakout_t s_breakout;

static cads_view_t s_dodger_view;
static cads_game_dodger_t s_dodger;

/* --- select screen ---------------------------------------------------- */

static void cads_game_select_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorWhite);

    for(uint32_t i = 0u; i < CADS_GAME_CARTRIDGE_COUNT; i++) {
        cads_rect_t row = {
            area.x, (int16_t)(area.y + (int16_t)i * CADS_GAME_SELECT_ROW_HEIGHT), area.width,
            CADS_GAME_SELECT_ROW_HEIGHT};
        bool selected = (i == s_select.selected);
        if(selected) cads_canvas_fill_rect(row.x, row.y, row.width, row.height, CadsColorBrandLight);
        cads_canvas_draw_text_aligned(
            row, CadsAlignCenter, &cads_font16, cads_game_cartridges[i].name,
            selected ? CadsColorBrand : CadsColorGrayDark);
    }
}

static bool cads_game_select_input(const cads_input_event_t* event, void* context) {
    (void)context;

    if(event->type == CadsInputPress && event->key == CadsKeyUp) {
        s_select.selected =
            (s_select.selected == 0u) ? CADS_GAME_CARTRIDGE_COUNT - 1u : s_select.selected - 1u;
        cads_view_dirty(&s_select.view);
        return true;
    }
    if(event->type == CadsInputPress && event->key == CadsKeyDown) {
        s_select.selected = (s_select.selected + 1u) % CADS_GAME_CARTRIDGE_COUNT;
        cads_view_dirty(&s_select.view);
        return true;
    }
    if(event->type == CadsInputRelease && event->key == CadsKeyOk) {
        cads_view_dispatcher_push(s_select.dispatcher, cads_game_cartridges[s_select.selected].view_id);
        return true;
    }
    return false; /* Back: unconsumed on purpose, pops to whatever opened the arcade */
}

static const cads_softkey_t cads_game_select_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Play"},
    {CadsKeyBack, "Back"},
};

/* --- reflex cartridge --------------------------------------------------- */

static void cads_game_reflex_view_draw(cads_rect_t area, void* context) {
    cads_game_reflex_draw(area, (const cads_game_reflex_t*)context);
}
static bool cads_game_reflex_view_input(const cads_input_event_t* event, void* context) {
    return cads_game_reflex_input(event, (cads_game_reflex_t*)context);
}
static void cads_game_reflex_view_enter(void* context) {
    cads_game_reflex_reset((cads_game_reflex_t*)context, cads_hal_ticks_ms());
}

static const cads_softkey_t cads_game_reflex_keys[] = {
    {CadsKeyOk, "Go"},
    {CadsKeyBack, "Back"},
};

/* --- snake cartridge ----------------------------------------------------- */

static void cads_game_snake_view_draw(cads_rect_t area, void* context) {
    cads_game_snake_draw(area, (const cads_game_snake_t*)context);
}
static bool cads_game_snake_view_input(const cads_input_event_t* event, void* context) {
    return cads_game_snake_input(event, (cads_game_snake_t*)context, cads_hal_ticks_ms());
}
static void cads_game_snake_view_enter(void* context) {
    cads_game_snake_reset((cads_game_snake_t*)context, cads_view_area(&s_snake_view), cads_hal_ticks_ms());
}

static const cads_softkey_t cads_game_snake_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyLeft, "Left"},
    {CadsKeyRight, "Right"},
    {CadsKeyBack, "Back"},
};

/* --- breakout cartridge --------------------------------------------------- */

static void cads_game_breakout_view_draw(cads_rect_t area, void* context) {
    cads_game_breakout_draw(area, (const cads_game_breakout_t*)context);
}
static bool cads_game_breakout_view_input(const cads_input_event_t* event, void* context) {
    return cads_game_breakout_input(event, (cads_game_breakout_t*)context, cads_hal_ticks_ms());
}
static void cads_game_breakout_view_enter(void* context) {
    cads_game_breakout_reset(
        (cads_game_breakout_t*)context, cads_view_area(&s_breakout_view), cads_hal_ticks_ms());
}

static const cads_softkey_t cads_game_breakout_keys[] = {
    {CadsKeyLeft, "Left"},
    {CadsKeyRight, "Right"},
    {CadsKeyBack, "Back"},
};

/* --- dodger cartridge ------------------------------------------------------ */

static void cads_game_dodger_view_draw(cads_rect_t area, void* context) {
    cads_game_dodger_draw(area, (const cads_game_dodger_t*)context);
}
static bool cads_game_dodger_view_input(const cads_input_event_t* event, void* context) {
    return cads_game_dodger_input(event, (cads_game_dodger_t*)context, cads_hal_ticks_ms());
}
static void cads_game_dodger_view_enter(void* context) {
    cads_game_dodger_reset((cads_game_dodger_t*)context, cads_view_area(&s_dodger_view), cads_hal_ticks_ms());
}

static const cads_softkey_t cads_game_dodger_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyBack, "Back"},
};

/* --- lifecycle ------------------------------------------------------------- */

void cads_game_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_select.dispatcher = dispatcher;
    s_select.selected = 0u;
    cads_view_init(&s_select.view, cads_game_select_draw, cads_game_select_input, NULL);
    cads_view_set_title(&s_select.view, "Leo's Arcade");
    cads_view_set_softkeys(
        &s_select.view, cads_game_select_keys,
        sizeof(cads_game_select_keys) / sizeof(cads_game_select_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_GAME, &s_select.view);

    cads_view_init(&s_reflex_view, cads_game_reflex_view_draw, cads_game_reflex_view_input, &s_reflex);
    cads_view_set_lifecycle(&s_reflex_view, cads_game_reflex_view_enter, NULL);
    cads_view_set_title(&s_reflex_view, "Reflex Test");
    cads_view_set_softkeys(
        &s_reflex_view, cads_game_reflex_keys, sizeof(cads_game_reflex_keys) / sizeof(cads_game_reflex_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_GAME_REFLEX, &s_reflex_view);

    cads_view_init(&s_snake_view, cads_game_snake_view_draw, cads_game_snake_view_input, &s_snake);
    cads_view_set_lifecycle(&s_snake_view, cads_game_snake_view_enter, NULL);
    cads_view_set_title(&s_snake_view, "Snake");
    cads_view_set_softkeys(
        &s_snake_view, cads_game_snake_keys, sizeof(cads_game_snake_keys) / sizeof(cads_game_snake_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_GAME_SNAKE, &s_snake_view);

    cads_view_init(
        &s_breakout_view, cads_game_breakout_view_draw, cads_game_breakout_view_input, &s_breakout);
    cads_view_set_lifecycle(&s_breakout_view, cads_game_breakout_view_enter, NULL);
    cads_view_set_title(&s_breakout_view, "Breakout");
    cads_view_set_softkeys(
        &s_breakout_view, cads_game_breakout_keys,
        sizeof(cads_game_breakout_keys) / sizeof(cads_game_breakout_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_GAME_BREAKOUT, &s_breakout_view);

    cads_view_init(&s_dodger_view, cads_game_dodger_view_draw, cads_game_dodger_view_input, &s_dodger);
    cads_view_set_lifecycle(&s_dodger_view, cads_game_dodger_view_enter, NULL);
    cads_view_set_title(&s_dodger_view, "Dodger");
    cads_view_set_softkeys(
        &s_dodger_view, cads_game_dodger_keys, sizeof(cads_game_dodger_keys) / sizeof(cads_game_dodger_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_GAME_DODGER, &s_dodger_view);

    s_game_ready = true;
}

void cads_game_tick(uint32_t now_ms) {
    if(!s_game_ready) return;

    uint32_t current = cads_view_dispatcher_current_id(s_select.dispatcher);
    bool changed = false;
    cads_view_t* view = NULL;

    switch(current) {
        case CADS_VIEW_ID_GAME_REFLEX:
            changed = cads_game_reflex_tick(&s_reflex, now_ms);
            view = &s_reflex_view;
            break;
        case CADS_VIEW_ID_GAME_SNAKE:
            changed = cads_game_snake_tick(&s_snake, cads_view_area(&s_snake_view), now_ms);
            view = &s_snake_view;
            break;
        case CADS_VIEW_ID_GAME_BREAKOUT:
            changed = cads_game_breakout_tick(&s_breakout, cads_view_area(&s_breakout_view), now_ms);
            view = &s_breakout_view;
            break;
        case CADS_VIEW_ID_GAME_DODGER:
            changed = cads_game_dodger_tick(&s_dodger, cads_view_area(&s_dodger_view), now_ms);
            view = &s_dodger_view;
            break;
        default:
            return; /* select screen, or the arcade is not on screen at all */
    }

    if(changed) cads_view_dirty(view);
}
