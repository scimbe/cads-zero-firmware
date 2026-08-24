/*
 * gui/widgets/cads_menu.c's row rendering on the host canvas.
 *
 * Exists because a hardware photo of the file browser showed its default-
 * selected row drawn in the *unselected* style (no brand fill, no green rail),
 * which should be impossible: a freshly populated list selects row 0, and
 * cads_menu_draw_row fills the selected row with the brand colour, a white
 * label and a green focus rail. This reproduces that exact path deterministically
 * - build a menu, draw it, read the pixels back - so "does the selected row
 * actually render selected" is answered by the canvas, not by a blurry webcam.
 */

#include <stddef.h>

#include "unity.h"

#include "canvas.h"
#include "cads_menu.h"

static cads_menu_t menu;
static const cads_menu_item_t items[] = {
    {"Alpha", NULL, 0u},
    {"Bravo", NULL, 1u},
    {"Charlie", NULL, 2u},
};

void setUp(void) {
    cads_canvas_init();
    cads_canvas_flush();
    cads_menu_init(&menu, items, sizeof(items) / sizeof(items[0]), &cads_font16);
    cads_rect_t area = {0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT};
    cads_menu_set_area(&menu, area);
}

void tearDown(void) {
}

static int16_t row_height(void) {
    return (int16_t)(cads_font16.line_height + 6 /* CADS_LIST_ROW_PADDING */);
}

/* The default selection is row 0, and it must render selected: a green rail
 * down the left edge and a brand fill behind it. */
static void test_default_selection_row0_renders_selected(void) {
    cads_menu_draw(&menu);

    /* x in [0,5) is the rail; a few pixels down into row 0. */
    TEST_ASSERT_EQUAL(CadsColorAccent, cads_canvas_get_pixel(2, 3));
    /* Just right of the rail, above the glyph baseline: the brand fill. */
    TEST_ASSERT_EQUAL(CadsColorBrand, cads_canvas_get_pixel(200, 2));
}

/* An unselected row below it is the light content surface, not brand. */
static void test_unselected_row_is_surface(void) {
    cads_menu_draw(&menu);

    int16_t y1 = (int16_t)(row_height() + 2);
    TEST_ASSERT_EQUAL(CadsColorSurface, cads_canvas_get_pixel(200, y1));
    /* No green rail on an unselected row. */
    TEST_ASSERT_EQUAL(CadsColorSurface, cads_canvas_get_pixel(2, y1));
}

/* Moving the selection moves the brand fill + rail to the new row. */
static void test_selection_follows_set_selected(void) {
    cads_menu_set_selected(&menu, 1u);
    cads_menu_draw(&menu);

    int16_t y1 = (int16_t)(row_height() + 3);
    TEST_ASSERT_EQUAL(CadsColorAccent, cads_canvas_get_pixel(2, y1));
    TEST_ASSERT_EQUAL(CadsColorBrand, cads_canvas_get_pixel(200, (int16_t)(row_height() + 2)));
    /* Row 0 is now unselected -> surface. */
    TEST_ASSERT_EQUAL(CadsColorSurface, cads_canvas_get_pixel(200, 2));
}

/* The file browser's exact sequence: init with zero items, set the area, then
 * populate via set_items - reflecting cads_filebrowser_enter -> refresh. A
 * hardware photo showed this path leaving row 0 unhighlighted; reproduce it. */
static void test_populate_after_empty_still_selects_row0(void) {
    cads_menu_t m;
    cads_menu_init(&m, NULL, 0u, &cads_font16); /* browser inits empty */
    cads_rect_t area = {0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT};
    cads_menu_set_area(&m, area); /* enter() */
    cads_menu_set_items(&m, items, sizeof(items) / sizeof(items[0])); /* refresh() */
    cads_menu_draw(&m);

    TEST_ASSERT_EQUAL(CadsColorAccent, cads_canvas_get_pixel(2, 3));
    TEST_ASSERT_EQUAL(CadsColorBrand, cads_canvas_get_pixel(200, 2));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_selection_row0_renders_selected);
    RUN_TEST(test_unselected_row_is_surface);
    RUN_TEST(test_selection_follows_set_selected);
    RUN_TEST(test_populate_after_empty_still_selects_row0);
    return UNITY_END();
}
