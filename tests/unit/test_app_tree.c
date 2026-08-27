/*
 * The full M6 app tree, registered exactly the way
 * apps/bringup/explorer_app_demo.c does it (same two calls, same order),
 * with a synthetic OK keypress routed through the real dispatcher/desktop
 * code - no display, no real button, no webcam needed.
 *
 * This exists because that exact setup had a real, silent bug: its view
 * array's capacity (7) was sized for the app tree as it stood before
 * apps/filebrowser was added, and was never updated - cads_view_dispatcher_
 * add() fails silently past capacity, so the LAST TWO registrations
 * (filebrowser's info view, and the MENU view itself) never actually
 * happened. Every `d` hardware check this session ran only asserted "no
 * fault, N frames flushed" - none of them pressed a button, so none of
 * them could have caught "the OK button that opens the menu does
 * nothing". test_menu_is_reachable_from_desktop() below is the same
 * scenario, host-side: it would have failed against the old capacity of
 * 7 and passes against the fixed value of 10.
 */

#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "cads_about.h"
#include "cads_active.h"
#include "cads_desktop.h"
#include "cads_filebrowser.h"
#include "cads_game.h"
#include "cads_gpio.h"
#include "cads_menu_app.h"
#include "cads_netinfo.h"
#include "cads_settings.h"
#include "cads_view_dispatcher.h"
#include "fake_hal.h"
#include "input/cads_input.h"

/* Mirrors apps/bringup/explorer_app_demo.c's own CADS_APP_DEMO_VIEW_CAPACITY
 * (24) and CADS_APP_DEMO_STACK_DEPTH (4) - kept as separate literals rather
 * than a shared header because the two are otherwise unrelated translation
 * units and a shared constant would be the only reason to couple them; see
 * that file's own comment for exactly what the 24 counts (the M9 Active Net
 * Tools suite's selector + one shared tool view are the +2 over the 22 the
 * rest of the tree already filled). */
#define VIEW_CAPACITY 26u
#define STACK_DEPTH   4u

static cads_view_entry_t s_entries[VIEW_CAPACITY];
static uint32_t s_stack[STACK_DEPTH];
static cads_view_dispatcher_t s_dispatcher;

void setUp(void) {
    cads_fake_reset();

    cads_view_dispatcher_init(&s_dispatcher, s_entries, VIEW_CAPACITY, s_stack, STACK_DEPTH);
    cads_rect_t full = {0, 0, CADS_DISPLAY_WIDTH, CADS_DISPLAY_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);

    cads_desktop_init(&s_dispatcher);
    cads_menu_app_init(&s_dispatcher); /* also registers settings, about, gpio, netinfo, filebrowser, game */

    TEST_ASSERT_TRUE(cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_DESKTOP));
}

void tearDown(void) {
}

static void press_ok(void) {
    cads_input_event_t event = {
        .type = CadsInputRelease,
        .key = CadsKeyOk,
        .x = 0,
        .y = 0,
        .timestamp = 1000u,
        .hold_ms = 0u,
    };
    cads_view_dispatcher_input(&s_dispatcher, &event);
}

static void test_every_app_tree_view_registers(void) {
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_DESKTOP));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_MENU));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_SETTINGS));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_SETTINGS_CONFIRM));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_ABOUT));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_GPIO));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_NETINFO));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_FILEBROWSER));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_FILEBROWSER_INFO));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_GAME));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_GAME_REFLEX));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_GAME_SNAKE));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_GAME_BREAKOUT));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_GAME_DODGER));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_ACTIVE));
    TEST_ASSERT_NOT_NULL(cads_view_dispatcher_find(&s_dispatcher, CADS_VIEW_ID_ACTIVE_TOOL));
}

static void test_menu_is_reachable_from_desktop(void) {
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_DESKTOP, cads_view_dispatcher_current_id(&s_dispatcher));

    press_ok();

    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_MENU, cads_view_dispatcher_current_id(&s_dispatcher));
}

/* The exact scenario the stale capacity constant broke: with too little
 * room, cads_view_dispatcher_add() silently drops the last registrations
 * attempted rather than growing or asserting - so a too-small capacity
 * looks identical to a correct one until something tries to navigate to
 * whichever view lost the coin flip. */
static void test_capacity_too_small_silently_drops_the_menu_view(void) {
    cads_view_entry_t small_entries[7];
    uint32_t small_stack[STACK_DEPTH];
    cads_view_dispatcher_t small_dispatcher;

    cads_view_dispatcher_init(&small_dispatcher, small_entries, 7u, small_stack, STACK_DEPTH);
    cads_rect_t full = {0, 0, CADS_DISPLAY_WIDTH, CADS_DISPLAY_HEIGHT};
    cads_view_dispatcher_set_area(&small_dispatcher, full);

    /* Fresh registration into a second dispatcher - cads_desktop_init()/
     * cads_menu_app_init() overwrite their own static view state each
     * call, which is exactly why this needs its own dispatcher rather
     * than reusing setUp()'s (that one already holds the real views,
     * registered against the real 10-slot table before this test body
     * ever runs). */
    cads_desktop_init(&small_dispatcher);
    cads_menu_app_init(&small_dispatcher);

    TEST_ASSERT_NULL(cads_view_dispatcher_find(&small_dispatcher, CADS_VIEW_ID_MENU));
}

static void test_game_is_reachable_from_the_menu(void) {
    press_ok(); /* desktop -> menu */
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_MENU, cads_view_dispatcher_current_id(&s_dispatcher));

    TEST_ASSERT_TRUE(cads_view_dispatcher_push(&s_dispatcher, CADS_VIEW_ID_GAME));
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_GAME, cads_view_dispatcher_current_id(&s_dispatcher));
}

/* The exact scenario a too-small capacity would silently break one level
 * deeper than test_capacity_too_small_silently_drops_the_menu_view checks:
 * pressing OK on the arcade's own select screen (default selection, the
 * Reflex Test cartridge) must actually push CADS_VIEW_ID_GAME_REFLEX, and
 * Back from there must return to the select screen rather than popping
 * past it - the real navigation this file's own header explains was
 * chosen specifically so each cartridge gets its own soft-key strip. */
static void test_a_cartridge_is_reachable_from_the_arcade_select_screen(void) {
    press_ok();                                        /* desktop -> menu */
    TEST_ASSERT_TRUE(cads_view_dispatcher_push(&s_dispatcher, CADS_VIEW_ID_GAME));

    press_ok(); /* select screen's default selection -> its cartridge */

    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_GAME_REFLEX, cads_view_dispatcher_current_id(&s_dispatcher));
    TEST_ASSERT_EQUAL_size_t(4u, cads_view_dispatcher_depth(&s_dispatcher));

    cads_input_event_t back = {
        .type = CadsInputRelease, .key = CadsKeyBack, .x = 0, .y = 0, .timestamp = 2000u, .hold_ms = 0u};
    cads_view_dispatcher_input(&s_dispatcher, &back);

    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_GAME, cads_view_dispatcher_current_id(&s_dispatcher));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_app_tree_view_registers);
    RUN_TEST(test_menu_is_reachable_from_desktop);
    RUN_TEST(test_capacity_too_small_silently_drops_the_menu_view);
    RUN_TEST(test_game_is_reachable_from_the_menu);
    RUN_TEST(test_a_cartridge_is_reachable_from_the_arcade_select_screen);
    return UNITY_END();
}
