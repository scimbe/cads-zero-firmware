/*
 * gui/canvas.c on the host, against the recording HAL in fake_hal.c.
 *
 * Two things here are worth more than the rest: the 4 bpp nibble packing,
 * where an off-by-one shows up as a subtly mirrored image and nothing else,
 * and the damage tracking, whose whole purpose is that a flush costs less than
 * a full screen - which is invisible unless something records what was pushed.
 */

#include <stddef.h>
#include <stdio.h>

#include "unity.h"

#include "canvas.h"
#include "fake_hal.h"

/* A rectangle in the sense the tests use: half open, x1/y1 exclusive. */
typedef struct {
    int16_t x0, y0, x1, y1;
    bool valid;
} bounds_t;

void setUp(void) {
    cads_canvas_init();
    /* init damages the whole screen; get that out of the way so every test
     * starts from a canvas whose damage is exactly what the test caused. */
    cads_canvas_flush();
    cads_fake_reset();
}

void tearDown(void) {
}

/** Bounding box of every pixel that is not `background`. */
static bounds_t painted_bounds(cads_color_t background) {
    bounds_t box = {0, 0, 0, 0, false};

    for(int16_t y = 0; y < CADS_CANVAS_HEIGHT; y++) {
        for(int16_t x = 0; x < CADS_CANVAS_WIDTH; x++) {
            if(cads_canvas_get_pixel(x, y) == background) continue;
            if(!box.valid) {
                box.x0 = x;
                box.y0 = y;
                box.x1 = (int16_t)(x + 1);
                box.y1 = (int16_t)(y + 1);
                box.valid = true;
                continue;
            }
            if(x < box.x0) box.x0 = x;
            if(y < box.y0) box.y0 = y;
            if(x >= box.x1) box.x1 = (int16_t)(x + 1);
            if(y >= box.y1) box.y1 = (int16_t)(y + 1);
        }
    }
    return box;
}

static void assert_blit_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    cads_fake_blit_t bounds;
    TEST_ASSERT_TRUE_MESSAGE(cads_fake_blit_bounds(&bounds), "nothing was blitted");
    TEST_ASSERT_EQUAL_UINT16(x, bounds.x);
    TEST_ASSERT_EQUAL_UINT16(y, bounds.y);
    TEST_ASSERT_EQUAL_UINT16(width, bounds.width);
    TEST_ASSERT_EQUAL_UINT16(height, bounds.height);
}

/* --- pixel packing ------------------------------------------------------- */

static void test_every_colour_round_trips_at_both_nibbles(void) {
    cads_canvas_clear(CadsColorBlack);

    /* Both parities of x, because the packing puts even pixels in the high
     * nibble and odd ones in the low nibble of the same byte. */
    for(int16_t x = 0; x < 32; x++) {
        cads_canvas_set_pixel(x, 5, (cads_color_t)(x % 16));
    }
    for(int16_t x = 0; x < 32; x++) {
        TEST_ASSERT_EQUAL_INT((cads_color_t)(x % 16), cads_canvas_get_pixel(x, 5));
    }

    /* And the packing itself: the byte layout is a contract with the flush
     * path, with draw_bitmap4() and with the DMA2D L4 mode M1 will use. */
    const uint8_t* buffer = cads_canvas_buffer();
    TEST_ASSERT_EQUAL_HEX8(0x01u, buffer[5u * CADS_CANVAS_STRIDE + 0u]);
    TEST_ASSERT_EQUAL_HEX8(0x23u, buffer[5u * CADS_CANVAS_STRIDE + 1u]);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, buffer[5u * CADS_CANVAS_STRIDE + 7u]);
}

static void test_writing_one_pixel_leaves_its_neighbour_alone(void) {
    cads_canvas_clear(CadsColorWhite);
    cads_canvas_set_pixel(4, 9, CadsColorRed);

    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(3, 9));
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(4, 9));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(5, 9));

    cads_canvas_set_pixel(7, 9, CadsColorTeal);
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(6, 9));
    TEST_ASSERT_EQUAL_INT(CadsColorTeal, cads_canvas_get_pixel(7, 9));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(8, 9));
}

static void test_pixels_outside_the_canvas_are_refused(void) {
    cads_canvas_clear(CadsColorWhite);

    cads_canvas_set_pixel(-1, 5, CadsColorRed);
    cads_canvas_set_pixel(CADS_CANVAS_WIDTH, 5, CadsColorRed);
    cads_canvas_set_pixel(5, -1, CadsColorRed);
    cads_canvas_set_pixel(5, CADS_CANVAS_HEIGHT, CadsColorRed);

    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(-1, 5));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(CADS_CANVAS_WIDTH, 5));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(0, 5));
}

/* --- fill_rect ------------------------------------------------------------ */

static void test_fill_rect_handles_ragged_edges_on_both_sides(void) {
    /* Odd x and odd width: a ragged nibble at each end around the memset. */
    cads_canvas_clear(CadsColorWhite);
    cads_canvas_fill_rect(3, 10, 7, 2, CadsColorAccent);

    for(int16_t y = 10; y < 12; y++) {
        TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(2, y));
        for(int16_t x = 3; x < 10; x++) {
            TEST_ASSERT_EQUAL_INT(CadsColorAccent, cads_canvas_get_pixel(x, y));
        }
        TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(10, y));
    }

    /* The rows either side must be untouched, including the nibbles that
     * share a byte with the filled ones. */
    for(int16_t x = 0; x < 12; x++) {
        TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(x, 9));
        TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(x, 12));
    }
}

static void test_fill_rect_covers_every_alignment_case(void) {
    /* even/even, even/odd, odd/even, odd/odd, and a single pixel at each
     * parity: between them these hit every branch around the memset. */
    static const struct {
        int16_t x;
        int16_t width;
    } cases[] = {{4, 4}, {4, 5}, {5, 4}, {5, 5}, {6, 1}, {7, 1}, {8, 2}, {9, 2}};

    for(size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cads_canvas_clear(CadsColorWhite);
        cads_canvas_fill_rect(cases[i].x, 20, cases[i].width, 1, CadsColorRed);

        for(int16_t x = 0; x < 16; x++) {
            bool inside = x >= cases[i].x && x < cases[i].x + cases[i].width;
            char note[64];
            snprintf(note, sizeof(note), "x=%d in fill(%d,%d)", x, cases[i].x, cases[i].width);
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                inside ? CadsColorRed : CadsColorWhite, cads_canvas_get_pixel(x, 20), note);
        }
    }
}

static void test_fill_rect_ignores_empty_and_inverted_rectangles(void) {
    cads_canvas_clear(CadsColorWhite);

    cads_canvas_fill_rect(10, 10, 0, 5, CadsColorRed);
    cads_canvas_fill_rect(10, 10, 5, 0, CadsColorRed);
    cads_canvas_fill_rect(10, 10, -5, 5, CadsColorRed);

    TEST_ASSERT_FALSE(cads_canvas_is_dirty());
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(10, 10));
}

static void test_fill_rect_is_clipped_to_the_canvas(void) {
    cads_canvas_clear(CadsColorWhite);
    cads_canvas_fill_rect(-5, -5, 10, 10, CadsColorRed);

    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(0, 0));
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(4, 4));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(5, 5));
}

static void test_outline_shapes_touch_only_their_edges(void) {
    cads_canvas_clear(CadsColorWhite);
    cads_canvas_draw_rect(3, 3, 5, 4, CadsColorBrand);

    TEST_ASSERT_EQUAL_INT(CadsColorBrand, cads_canvas_get_pixel(3, 3));
    TEST_ASSERT_EQUAL_INT(CadsColorBrand, cads_canvas_get_pixel(7, 3));
    TEST_ASSERT_EQUAL_INT(CadsColorBrand, cads_canvas_get_pixel(3, 6));
    TEST_ASSERT_EQUAL_INT(CadsColorBrand, cads_canvas_get_pixel(7, 6));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(5, 5));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(8, 3));

    cads_canvas_clear(CadsColorWhite);
    cads_canvas_draw_hline(2, 40, 3, CadsColorRed);
    cads_canvas_draw_vline(20, 41, 3, CadsColorTeal);
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(4, 40));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(5, 40));
    TEST_ASSERT_EQUAL_INT(CadsColorTeal, cads_canvas_get_pixel(20, 43));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(20, 44));
}

static void test_lines_reach_both_endpoints(void) {
    cads_canvas_clear(CadsColorBlack);
    cads_canvas_draw_line(10, 10, 20, 15, CadsColorWhite);

    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(10, 10));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(20, 15));

    /* A degenerate line is one pixel, not an infinite loop. */
    cads_canvas_draw_line(30, 30, 30, 30, CadsColorRed);
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(30, 30));
}

/* --- clipping -------------------------------------------------------------- */

static void test_clipping_confines_drawing(void) {
    cads_canvas_clear(CadsColorBlack);

    cads_rect_t clip = {100, 100, 50, 50};
    cads_canvas_push_clip(clip);
    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT, CadsColorRed);
    cads_canvas_pop_clip();

    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(99, 120));
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(100, 120));
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(149, 149));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(150, 120));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(120, 150));
}

static void test_nested_clips_intersect_and_unwind(void) {
    cads_canvas_clear(CadsColorBlack);

    cads_rect_t outer = {100, 100, 50, 50};
    cads_rect_t inner = {120, 90, 100, 100}; /* overhangs the outer clip */
    cads_canvas_push_clip(outer);
    cads_canvas_push_clip(inner);

    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT, CadsColorRed);

    /* The intersection is (120,100)..(150,150): the child never widens the
     * parent, whichever side it overhangs. */
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(119, 120));
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(120, 120));
    TEST_ASSERT_EQUAL_INT(CadsColorRed, cads_canvas_get_pixel(149, 149));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(120, 99));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(150, 120));

    /* Popping the inner clip restores the outer one exactly. */
    cads_canvas_pop_clip();
    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT, CadsColorAccent);
    TEST_ASSERT_EQUAL_INT(CadsColorAccent, cads_canvas_get_pixel(100, 100));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(99, 100));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(150, 150));

    /* And popping the outer one restores the whole canvas. */
    cads_canvas_pop_clip();
    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT, CadsColorWhite);
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(0, 0));
    TEST_ASSERT_EQUAL_INT(
        CadsColorWhite, cads_canvas_get_pixel(CADS_CANVAS_WIDTH - 1, CADS_CANVAS_HEIGHT - 1));
}

static void test_an_empty_clip_draws_nothing(void) {
    cads_canvas_clear(CadsColorBlack);

    cads_rect_t left = {0, 0, 50, 50};
    cads_rect_t right = {100, 0, 50, 50};
    cads_canvas_push_clip(left);
    cads_canvas_push_clip(right); /* disjoint: nothing may be drawn */

    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT, CadsColorRed);
    cads_canvas_set_pixel(10, 10, CadsColorRed);
    cads_canvas_set_pixel(110, 10, CadsColorRed);

    TEST_ASSERT_FALSE(cads_canvas_is_dirty());

    cads_canvas_pop_clip();
    cads_canvas_pop_clip();
}

/* --- damage tracking and flush --------------------------------------------- */

static void test_a_clean_canvas_flushes_nothing(void) {
    TEST_ASSERT_FALSE(cads_canvas_is_dirty());
    TEST_ASSERT_EQUAL_UINT32(0u, cads_canvas_flush());
    TEST_ASSERT_EQUAL_UINT32(0u, cads_fake_blit_count());
}

static void test_damage_is_the_bounding_box_of_everything_drawn(void) {
    cads_canvas_set_pixel(10, 10, CadsColorRed);
    cads_canvas_set_pixel(20, 30, CadsColorRed);
    TEST_ASSERT_TRUE(cads_canvas_is_dirty());

    /* One box, not two: the canvas tracks a bounding box, so the pixels
     * between the two marks are transferred as well. */
    TEST_ASSERT_EQUAL_UINT32(11u * 21u, cads_canvas_flush());
    assert_blit_rect(10, 10, 11, 21);
    TEST_ASSERT_FALSE(cads_canvas_is_dirty());
}

static void test_a_dirty_rectangle_limits_the_transfer(void) {
    cads_canvas_fill_rect(200, 200, 40, 40, CadsColorMagenta);

    TEST_ASSERT_EQUAL_UINT32(40u * 40u, cads_canvas_flush());
    TEST_ASSERT_EQUAL_UINT32(40u * 40u, cads_fake_blit_pixels());
    assert_blit_rect(200, 200, 40, 40);

    /* Comfortably inside one staging band, so it goes out in a single blit -
     * the panel is fed at 16 clocks per pixel, and a rectangle split into
     * needless bands pays a window command for each one. */
    TEST_ASSERT_EQUAL_UINT32(1u, cads_fake_blit_count());
}

static void test_a_full_screen_flush_transfers_every_pixel(void) {
    cads_canvas_clear(CadsColorBrand);

    uint32_t pixels = cads_canvas_flush();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)CADS_CANVAS_WIDTH * CADS_CANVAS_HEIGHT, pixels);
    TEST_ASSERT_EQUAL_UINT32(pixels, cads_fake_blit_pixels());
    assert_blit_rect(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT);

    /* Banded, because the staging buffers are 16 rows of 480. */
    TEST_ASSERT_EQUAL_UINT32(20u, cads_fake_blit_count());
    for(uint32_t i = 0u; i < cads_fake_blit_count(); i++) {
        const cads_fake_blit_t* blit = cads_fake_blit_at(i);
        TEST_ASSERT_NOT_NULL(blit);
        TEST_ASSERT_EQUAL_UINT16(0u, blit->x);
        TEST_ASSERT_EQUAL_UINT16(CADS_CANVAS_WIDTH, blit->width);
        TEST_ASSERT_EQUAL_UINT16(i * 16u, blit->y);
        TEST_ASSERT_EQUAL_UINT16(16u, blit->height);
    }
}

static void test_damage_outside_the_canvas_is_clamped(void) {
    cads_canvas_damage(-10, -10, 20, 20);
    TEST_ASSERT_EQUAL_UINT32(10u * 10u, cads_canvas_flush());
    assert_blit_rect(0, 0, 10, 10);

    cads_canvas_damage((int16_t)(CADS_CANVAS_WIDTH - 5), (int16_t)(CADS_CANVAS_HEIGHT - 5), 50, 50);
    TEST_ASSERT_EQUAL_UINT32(5u * 5u, cads_canvas_flush());

    /* Degenerate rectangles are not damage. */
    cads_canvas_damage(10, 10, 0, 10);
    cads_canvas_damage(10, 10, 10, -1);
    cads_canvas_damage((int16_t)CADS_CANVAS_WIDTH, 0, 10, 10);
    TEST_ASSERT_FALSE(cads_canvas_is_dirty());
}

static void test_flush_expands_through_the_palette(void) {
    /* The palette is held byte-swapped so the SPI DMA emits the high byte
     * first; the panel would show garbage otherwise. Pure red is 0xF800,
     * which reaches the wire - and the fake panel - as 0x00F8. */
    cads_canvas_set_palette(CadsColorRed, 0xFF, 0x00, 0x00);
    cads_canvas_set_palette(CadsColorBlack, 0x00, 0x00, 0x00);

    TEST_ASSERT_EQUAL_HEX16(0x00F8u, cads_canvas_palette_rgb565()[CadsColorRed]);

    cads_canvas_clear(CadsColorBlack);
    cads_canvas_fill_rect(3, 7, 5, 2, CadsColorRed); /* ragged on both edges */
    cads_canvas_flush();

    /* Odd and even source columns both have to land on the right pixel: the
     * expander reads one byte and writes two halfwords. */
    TEST_ASSERT_EQUAL_HEX16(0x0000u, cads_fake_panel_pixel(2, 7));
    TEST_ASSERT_EQUAL_HEX16(0x00F8u, cads_fake_panel_pixel(3, 7));
    TEST_ASSERT_EQUAL_HEX16(0x00F8u, cads_fake_panel_pixel(4, 7));
    TEST_ASSERT_EQUAL_HEX16(0x00F8u, cads_fake_panel_pixel(7, 8));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, cads_fake_panel_pixel(8, 7));
}

/* --- bitmaps ---------------------------------------------------------------- */

static void test_draw_bitmap4_unpacks_and_honours_transparency(void) {
    /* 3x2, rows padded to whole bytes: high nibble first. */
    static const uint8_t bitmap[] = {
        0x12, 0x30, /* 1 2 3 */
        0x45, 0x60, /* 4 5 6 */
    };

    cads_canvas_clear(CadsColorBlack);
    cads_canvas_draw_bitmap4(10, 10, 3, 2, bitmap, 16u); /* nothing transparent */

    TEST_ASSERT_EQUAL_INT(1, cads_canvas_get_pixel(10, 10));
    TEST_ASSERT_EQUAL_INT(2, cads_canvas_get_pixel(11, 10));
    TEST_ASSERT_EQUAL_INT(3, cads_canvas_get_pixel(12, 10));
    TEST_ASSERT_EQUAL_INT(4, cads_canvas_get_pixel(10, 11));
    TEST_ASSERT_EQUAL_INT(6, cads_canvas_get_pixel(12, 11));
    TEST_ASSERT_EQUAL_INT(CadsColorBlack, cads_canvas_get_pixel(13, 10));

    /* The padding nibble at the end of a row must never be drawn. */
    cads_canvas_clear(CadsColorWhite);
    cads_canvas_draw_bitmap4(10, 10, 3, 2, bitmap, 2u); /* index 2 see-through */
    TEST_ASSERT_EQUAL_INT(1, cads_canvas_get_pixel(10, 10));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(11, 10));
    TEST_ASSERT_EQUAL_INT(3, cads_canvas_get_pixel(12, 10));
    TEST_ASSERT_EQUAL_INT(CadsColorWhite, cads_canvas_get_pixel(13, 11));
}

/* --- text -------------------------------------------------------------------- */

static void test_text_width_is_the_sum_of_the_advances(void) {
    int16_t one = cads_canvas_text_width(&cads_font12, "M");
    TEST_ASSERT_GREATER_THAN_INT16(0, one);
    TEST_ASSERT_EQUAL_INT16(one * 4, cads_canvas_text_width(&cads_font12, "MMMM"));
    TEST_ASSERT_EQUAL_INT16(0, cads_canvas_text_width(&cads_font12, ""));

    /* Larger sizes are wider; a font table that got mixed up would not be. */
    TEST_ASSERT_GREATER_THAN_INT16(
        cads_canvas_text_width(&cads_font12, "CaDS"), cads_canvas_text_width(&cads_font24, "CaDS"));
}

static void test_characters_outside_the_baked_range_render_as_a_space(void) {
    /* Nothing in the font table covers a control character, and rendering a
     * garbage glyph or walking off the bitmap would both be worse. */
    int16_t space = cads_canvas_text_width(&cads_font12, " ");
    TEST_ASSERT_EQUAL_INT16(space, cads_canvas_text_width(&cads_font12, "\x01"));
    TEST_ASSERT_EQUAL_INT16(space, cads_canvas_text_width(&cads_font12, "\xE4"));

    cads_canvas_clear(CadsColorBlack);
    cads_canvas_draw_text(10, 10, &cads_font12, "\x01\x02", CadsColorWhite);
    TEST_ASSERT_FALSE(cads_canvas_is_dirty()); /* a space lights no pixels */
}

static void test_draw_text_returns_the_pen_and_damages_what_it_drew(void) {
    cads_canvas_clear(CadsColorBlack);
    cads_canvas_flush();
    cads_fake_reset();

    const int16_t x = 40;
    const int16_t y = 24;
    int16_t width = cads_canvas_text_width(&cads_font12, "Hg");
    int16_t pen = cads_canvas_draw_text(x, y, &cads_font12, "Hg", CadsColorWhite);
    TEST_ASSERT_EQUAL_INT16(x + width, pen);

    bounds_t lit = painted_bounds(CadsColorBlack);
    TEST_ASSERT_TRUE_MESSAGE(lit.valid, "draw_text lit no pixels at all");

    cads_fake_blit_t damaged;
    cads_canvas_flush();
    TEST_ASSERT_TRUE(cads_fake_blit_bounds(&damaged));

    /* Every pixel that was drawn has to reach the panel... */
    TEST_ASSERT_LESS_OR_EQUAL_INT16(lit.x0, (int16_t)damaged.x);
    TEST_ASSERT_LESS_OR_EQUAL_INT16(lit.y0, (int16_t)damaged.y);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(lit.x1, (int16_t)(damaged.x + damaged.width));
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(lit.y1, (int16_t)(damaged.y + damaged.height));

    /* ...and no more than the line box the run occupies, or the "one rectangle
     * per run" optimisation would be costing more than the per-pixel damage it
     * replaced. */
    TEST_ASSERT_EQUAL_INT16(x, (int16_t)damaged.x);
    TEST_ASSERT_EQUAL_INT16(width, (int16_t)damaged.width);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(y, (int16_t)damaged.y);
    TEST_ASSERT_LESS_OR_EQUAL_INT16(
        (int16_t)(y + cads_font12.line_height), (int16_t)(damaged.y + damaged.height));
}

static void test_draw_text_is_clipped(void) {
    cads_canvas_clear(CadsColorBlack);

    cads_rect_t box = {50, 50, 20, 20};
    cads_canvas_push_clip(box);
    cads_canvas_draw_text(40, 52, &cads_font16, "CaDS Zero", CadsColorWhite);
    cads_canvas_pop_clip();

    bounds_t lit = painted_bounds(CadsColorBlack);
    TEST_ASSERT_TRUE(lit.valid);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(50, lit.x0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(50, lit.y0);
    TEST_ASSERT_LESS_OR_EQUAL_INT16(70, lit.x1);
    TEST_ASSERT_LESS_OR_EQUAL_INT16(70, lit.y1);
}

static void test_aligned_text_places_the_run_inside_its_box(void) {
    cads_rect_t box = {100, 100, 200, 40};
    int16_t width = cads_canvas_text_width(&cads_font16, "CaDS");

    cads_canvas_clear(CadsColorBlack);
    cads_canvas_draw_text_aligned(box, CadsAlignLeft, &cads_font16, "CaDS", CadsColorWhite);
    bounds_t left = painted_bounds(CadsColorBlack);

    cads_canvas_clear(CadsColorBlack);
    cads_canvas_draw_text_aligned(box, CadsAlignCenter, &cads_font16, "CaDS", CadsColorWhite);
    bounds_t centre = painted_bounds(CadsColorBlack);

    cads_canvas_clear(CadsColorBlack);
    cads_canvas_draw_text_aligned(box, CadsAlignRight, &cads_font16, "CaDS", CadsColorWhite);
    bounds_t right = painted_bounds(CadsColorBlack);

    TEST_ASSERT_TRUE(left.valid && centre.valid && right.valid);
    TEST_ASSERT_LESS_THAN_INT16(centre.x0, left.x0);
    TEST_ASSERT_LESS_THAN_INT16(right.x0, centre.x0);

    /* Right alignment ends at the right edge of the box, give or take the
     * last glyph's bearing. */
    TEST_ASSERT_LESS_OR_EQUAL_INT16(300, right.x1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16((int16_t)(300 - width), right.x0);

    /* And every alignment stays inside the box vertically. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(100, centre.y0);
    TEST_ASSERT_LESS_OR_EQUAL_INT16(140, centre.y1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_colour_round_trips_at_both_nibbles);
    RUN_TEST(test_writing_one_pixel_leaves_its_neighbour_alone);
    RUN_TEST(test_pixels_outside_the_canvas_are_refused);
    RUN_TEST(test_fill_rect_handles_ragged_edges_on_both_sides);
    RUN_TEST(test_fill_rect_covers_every_alignment_case);
    RUN_TEST(test_fill_rect_ignores_empty_and_inverted_rectangles);
    RUN_TEST(test_fill_rect_is_clipped_to_the_canvas);
    RUN_TEST(test_outline_shapes_touch_only_their_edges);
    RUN_TEST(test_lines_reach_both_endpoints);
    RUN_TEST(test_clipping_confines_drawing);
    RUN_TEST(test_nested_clips_intersect_and_unwind);
    RUN_TEST(test_an_empty_clip_draws_nothing);
    RUN_TEST(test_a_clean_canvas_flushes_nothing);
    RUN_TEST(test_damage_is_the_bounding_box_of_everything_drawn);
    RUN_TEST(test_a_dirty_rectangle_limits_the_transfer);
    RUN_TEST(test_a_full_screen_flush_transfers_every_pixel);
    RUN_TEST(test_damage_outside_the_canvas_is_clamped);
    RUN_TEST(test_flush_expands_through_the_palette);
    RUN_TEST(test_draw_bitmap4_unpacks_and_honours_transparency);
    RUN_TEST(test_text_width_is_the_sum_of_the_advances);
    RUN_TEST(test_characters_outside_the_baked_range_render_as_a_space);
    RUN_TEST(test_draw_text_returns_the_pen_and_damages_what_it_drew);
    RUN_TEST(test_draw_text_is_clipped);
    RUN_TEST(test_aligned_text_places_the_run_inside_its_box);
    return UNITY_END();
}
