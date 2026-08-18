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

void cads_splash_draw(const char* status) {
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
}
