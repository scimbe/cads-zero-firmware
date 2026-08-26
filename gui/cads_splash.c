/*
 * CaDS Zero - boot screen.
 *
 * The CaDS mark carries the wordmark, the lion and the tagline already, so the
 * splash adds only what the mark does not say: which firmware this is, and what
 * it is currently doing.
 */

#include "cads_splash.h"

#include "assets/cads_assets.h"
#include "canvas.h"
#include "fonts/font.h"

/* Draw the mark + wordmark + status line, and return the y just below the
 * accent rule so a caller (the progress variant) can place a bar there. */
static int16_t cads_splash_draw_mark(const char* status) {
    cads_canvas_clear(CadsColorBackground);

    /* The mark, centred horizontally and sitting slightly above centre - text
     * below it needs more visual room than the space above. */
    int16_t logo_x = (int16_t)((CADS_CANVAS_WIDTH - cads_logo.width) / 2);
    int16_t logo_y = 24;
    cads_canvas_draw_image(logo_x, logo_y, &cads_logo);

    /* Accent rule in the brand green, matched to the wordmark's width rather
     * than the screen's, so it reads as part of the mark. */
    int16_t rule_y = (int16_t)(logo_y + cads_logo.height + 6);
    cads_canvas_fill_rect(logo_x, rule_y, cads_logo.width, 2, CadsColorAccent);

    cads_rect_t title = {0, (int16_t)(rule_y + 12), CADS_CANVAS_WIDTH, 34};
    cads_canvas_draw_text_aligned(
        title, CadsAlignCenter, &cads_font24, "Z E R O", CadsColorBrandLight);

    if(status) {
        cads_rect_t line = {
            0, (int16_t)(CADS_CANVAS_HEIGHT - 26), CADS_CANVAS_WIDTH, 20};
        cads_canvas_draw_text_aligned(line, CadsAlignCenter, &cads_font12, status, CadsColorGray);
    }
    return rule_y;
}

void cads_splash_draw(const char* status) {
    (void)cads_splash_draw_mark(status);
}

void cads_splash_draw_progress(const char* status, uint8_t percent) {
    (void)cads_splash_draw_mark(status);
    if(percent > 100u) percent = 100u;

    /* A slim progress bar under the wordmark: a track in the darker brand tone
     * with an accent-green fill. This is what the boot animates instead of a
     * raw test pattern, so "something is happening" reads at a glance. Redraws
     * clear the whole canvas (cads_splash_draw_mark), so successive frames
     * need no separate erase. */
    const int16_t bar_w = 220;
    const int16_t bar_h = 8;
    int16_t bar_x = (int16_t)((CADS_CANVAS_WIDTH - bar_w) / 2);
    int16_t bar_y = (int16_t)(CADS_CANVAS_HEIGHT - 60);
    cads_canvas_fill_rect(bar_x, bar_y, bar_w, bar_h, CadsColorBrandDark);
    int16_t fill_w = (int16_t)((int32_t)bar_w * percent / 100);
    if(fill_w > 0) cads_canvas_fill_rect(bar_x, bar_y, fill_w, bar_h, CadsColorAccent);
}

void cads_test_pattern_draw(void) {
    cads_canvas_clear(CadsColorBackground);

    /* Brand header. If the panel scan direction were mirrored this bar would
     * end up at the bottom, which is the whole point of having it. */
    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, 40, CadsColorBrand);
    cads_canvas_fill_rect(0, 40, CADS_CANVAS_WIDTH, 3, CadsColorAccent);

    /* Sixteen palette swatches. A wrong RGB565 byte order shows up here as
     * obviously wrong hues rather than a subtle tint. */
    const int16_t swatch_width = CADS_CANVAS_WIDTH / 16;
    for(int16_t i = 0; i < 16; i++) {
        cads_canvas_fill_rect((int16_t)(i * swatch_width), 60, swatch_width, 80, (cads_color_t)i);
    }

    /* Corner markers: proves the addressable area really is 480x320 and that
     * the window command is not off by one. */
    cads_canvas_fill_rect(0, 0, 8, 8, CadsColorRed);
    cads_canvas_fill_rect(CADS_CANVAS_WIDTH - 8, 0, 8, 8, CadsColorAccent);
    cads_canvas_fill_rect(0, CADS_CANVAS_HEIGHT - 8, 8, 8, CadsColorAmber);
    cads_canvas_fill_rect(CADS_CANVAS_WIDTH - 8, CADS_CANVAS_HEIGHT - 8, 8, 8, CadsColorTeal);

    /* Diagonals: any dropped or duplicated pixel in the blit path breaks the
     * straightness visibly. */
    cads_canvas_draw_line(0, 160, CADS_CANVAS_WIDTH - 1, CADS_CANVAS_HEIGHT - 1, CadsColorWhite);
    cads_canvas_draw_line(CADS_CANVAS_WIDTH - 1, 160, 0, CADS_CANVAS_HEIGHT - 1, CadsColorWhite);

    cads_canvas_draw_rect(4, 44, CADS_CANVAS_WIDTH - 8, CADS_CANVAS_HEIGHT - 48, CadsColorGrayLight);
}
