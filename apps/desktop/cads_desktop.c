#include "cads_desktop.h"

#include <stdbool.h>
#include <stdint.h>

#include "assets/cads_assets.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "canvas.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "input/cads_input.h"

#include "../menu/cads_menu_app.h"

/*
 * cads_canvas_draw_bitmap4() draws a buffer that IS the image at the given
 * width - there is no source stride, so it cannot blit a cropped slice of a
 * larger packed image. Leo's own pixels therefore cannot be "closed" by
 * re-drawing part of cads_leo without a second asset. The blink is instead a
 * pair of eye shapes drawn with plain canvas primitives on top of the
 * portrait, in one small fixed rectangle; each state (open/closed) is fully
 * opaque over that rectangle, so there is nothing to erase between them.
 *
 * The blink overlay does not sit on the lion artwork: on this panel the mane
 * fills the frame with no discernible face where the eyes would land, so two
 * rectangles in the middle of it read as noise, not a blink. It lives in the
 * top-right corner of the content area instead - a small blink widget clear of
 * the logo (user request, 2026-08-26). CADS_DESKTOP_EYES_MARGIN is the inset
 * from the content area's top and right edges.
 */
#define CADS_DESKTOP_EYES_MARGIN   8
#define CADS_DESKTOP_EYES_WIDTH    48
#define CADS_DESKTOP_EYES_HEIGHT   16

#define CADS_DESKTOP_PET_WIDTH    48
#define CADS_DESKTOP_PET_HEIGHT   20
#define CADS_DESKTOP_PET_FLASH_MS 600u

/** Leo goes quiet after this long without an interaction, regardless of level. */
#define CADS_LEO_IDLE_MS (3u * 60u * 1000u)

typedef enum {
    CADS_LEO_MOOD_SLEEPY = 0,
    CADS_LEO_MOOD_CONTENT,
    CADS_LEO_MOOD_HAPPY,
    CADS_LEO_MOOD_EXCITED,
} cads_leo_mood_t;

static const char* const cads_leo_mood_names[] = {"dozing", "content", "cheerful", "wired"};
static const cads_color_t cads_leo_mood_colors[] = {
    CadsColorGray, CadsColorTeal, CadsColorAccent, CadsColorAmber};

typedef struct {
    /*
     * Raw counters. modules/storage is being built concurrently and this app
     * does not depend on it yet; once it lands, load these three fields right
     * after cads_hal_init() and save them on change. Until then Leo's memory
     * lives only in .bss and resets on reboot.
     */
    uint32_t apps_opened;
    uint32_t interactions;
    uint32_t boot_ms;
    uint32_t last_interaction_ms;

    cads_leo_mood_t mood;

    bool eyes_closed;
    uint32_t next_blink_ms;
    uint32_t reopen_ms;
} cads_leo_state_t;

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_leo_state_t leo;

    cads_rect_t portrait_rect;
    cads_rect_t eyes_rect;
    cads_rect_t pet_rect;
    cads_rect_t caption_rect;

    bool need_full;
    bool need_eyes;
    bool need_pet;
    bool need_caption;

    bool pet_flash_active;
    uint32_t pet_flash_until_ms;
} cads_desktop_t;

static cads_desktop_t s_desktop;

static const cads_softkey_t cads_desktop_keys[] = {
    {CadsKeyOk, "Menu"},
    {CadsKeyF1, "Pet"},
};

/* --- mood ------------------------------------------------------------------ */

static uint32_t cads_desktop_level(const cads_leo_state_t* leo, uint32_t now_ms) {
    uint32_t uptime_minutes = (now_ms - leo->boot_ms) / 60000u;
    return leo->apps_opened * 3u + leo->interactions / 4u + uptime_minutes / 10u;
}

static cads_leo_mood_t cads_desktop_compute_mood(const cads_leo_state_t* leo, uint32_t now_ms) {
    if((int32_t)(now_ms - leo->last_interaction_ms) > (int32_t)CADS_LEO_IDLE_MS) {
        return CADS_LEO_MOOD_SLEEPY;
    }
    uint32_t level = cads_desktop_level(leo, now_ms);
    if(level >= 16u) return CADS_LEO_MOOD_EXCITED;
    if(level >= 8u) return CADS_LEO_MOOD_HAPPY;
    if(level >= 3u) return CADS_LEO_MOOD_CONTENT;
    return CADS_LEO_MOOD_SLEEPY;
}

/* Blink cadence follows the mood: dozing is slow with long closed eyes, wired
 * is quick with short ones. Small and honest rather than tuned against a real
 * lion. */
static uint32_t cads_desktop_blink_period_ms(cads_leo_mood_t mood) {
    switch(mood) {
        case CADS_LEO_MOOD_EXCITED: return 1800u;
        case CADS_LEO_MOOD_HAPPY: return 2600u;
        case CADS_LEO_MOOD_CONTENT: return 3600u;
        default: return 6000u;
    }
}

static uint32_t cads_desktop_blink_close_ms(cads_leo_mood_t mood) {
    return (mood == CADS_LEO_MOOD_SLEEPY) ? 450u : 150u;
}

static void cads_desktop_schedule_blink(cads_leo_state_t* leo, uint32_t now_ms) {
    /* now_ms folded into the period keeps blinks off a perfectly robotic
     * interval without pulling in a PRNG for one cosmetic wobble. */
    leo->next_blink_ms = now_ms + cads_desktop_blink_period_ms(leo->mood) + (now_ms % 700u);
}

/* --- layout ----------------------------------------------------------------- */

static void cads_desktop_layout(cads_desktop_t* app, cads_rect_t area) {
    app->portrait_rect.width = (int16_t)cads_leo.width;
    app->portrait_rect.height = (int16_t)cads_leo.height;
    app->portrait_rect.x = (int16_t)(area.x + (area.width - cads_leo.width) / 2);
    app->portrait_rect.y = (int16_t)(area.y + 12);

    app->eyes_rect.width = CADS_DESKTOP_EYES_WIDTH;
    app->eyes_rect.height = CADS_DESKTOP_EYES_HEIGHT;
    app->eyes_rect.x = (int16_t)(area.x + area.width - CADS_DESKTOP_EYES_WIDTH - CADS_DESKTOP_EYES_MARGIN);
    app->eyes_rect.y = (int16_t)(area.y + CADS_DESKTOP_EYES_MARGIN);

    app->pet_rect.width = CADS_DESKTOP_PET_WIDTH;
    app->pet_rect.height = CADS_DESKTOP_PET_HEIGHT;
    app->pet_rect.x =
        (int16_t)(app->portrait_rect.x + app->portrait_rect.width - CADS_DESKTOP_PET_WIDTH);
    app->pet_rect.y = app->portrait_rect.y;

    app->caption_rect.x = area.x;
    app->caption_rect.y = (int16_t)(app->portrait_rect.y + app->portrait_rect.height + 8);
    app->caption_rect.width = area.width;
    app->caption_rect.height = 44;
}

/* --- drawing ----------------------------------------------------------------- */

static void cads_desktop_paint_eyes(const cads_desktop_t* app) {
    cads_rect_t r = app->eyes_rect;
    cads_canvas_fill_rect(r.x, r.y, r.width, r.height, CadsColorBrandDark);

    if(app->leo.eyes_closed) {
        int16_t mid = (int16_t)(r.y + r.height / 2);
        cads_canvas_draw_hline(r.x, mid, r.width, CadsColorBlack);
        return;
    }

    int16_t eye_w = (int16_t)(r.width / 2 - 4);
    int16_t pupil = 4;
    cads_canvas_fill_rect(r.x, (int16_t)(r.y + 2), eye_w, (int16_t)(r.height - 4), CadsColorWhite);
    cads_canvas_fill_rect(
        (int16_t)(r.x + r.width - eye_w), (int16_t)(r.y + 2), eye_w, (int16_t)(r.height - 4),
        CadsColorWhite);
    cads_canvas_fill_rect(
        (int16_t)(r.x + eye_w / 2 - pupil / 2), (int16_t)(r.y + r.height / 2 - pupil / 2), pupil,
        pupil, CadsColorBlack);
    cads_canvas_fill_rect(
        (int16_t)(r.x + r.width - eye_w / 2 - pupil / 2),
        (int16_t)(r.y + r.height / 2 - pupil / 2), pupil, pupil, CadsColorBlack);
}

static void cads_desktop_paint_pet_badge(const cads_desktop_t* app) {
    cads_rect_t r = app->pet_rect;
    cads_canvas_fill_rect(r.x, r.y, r.width, r.height, CadsColorBackground);
    if(app->pet_flash_active) {
        cads_canvas_draw_text_aligned(r, CadsAlignCenter, &cads_font12, "+1", CadsColorAccent);
    }
}

static void cads_desktop_paint_caption(const cads_desktop_t* app) {
    cads_rect_t r = app->caption_rect;
    cads_canvas_fill_rect(r.x, r.y, r.width, r.height, CadsColorBackground);

    char line[48];
    uint32_t level = cads_desktop_level(&app->leo, cads_hal_ticks_ms());
    size_t pos = cads_str_copy(line, sizeof(line), "Leo is ");
    pos = cads_str_append(line, sizeof(line), cads_leo_mood_names[app->leo.mood]);
    pos = cads_str_append(line, sizeof(line), " - level ");
    if(pos < sizeof(line)) cads_fmt_uint(line + pos, sizeof(line) - pos, level);

    cads_rect_t top = {r.x, r.y, r.width, (int16_t)(r.height / 2)};
    cads_rect_t bottom = {r.x, (int16_t)(r.y + r.height / 2), r.width, (int16_t)(r.height / 2)};
    cads_canvas_draw_text_aligned(
        top, CadsAlignCenter, &cads_font16, line, cads_leo_mood_colors[app->leo.mood]);
    cads_canvas_draw_text_aligned(
        bottom, CadsAlignCenter, &cads_font12, "OK opens the menu - F1 pets Leo", CadsColorGray);
}

static void cads_desktop_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_desktop_t* app = (cads_desktop_t*)context;

    if(app->need_full) {
        cads_canvas_draw_image(app->portrait_rect.x, app->portrait_rect.y, &cads_leo);
        cads_desktop_paint_eyes(app);
        cads_desktop_paint_pet_badge(app);
        cads_desktop_paint_caption(app);
        app->need_full = false;
        app->need_eyes = false;
        app->need_pet = false;
        app->need_caption = false;
        return;
    }
    if(app->need_eyes) {
        cads_desktop_paint_eyes(app);
        app->need_eyes = false;
    }
    if(app->need_pet) {
        cads_desktop_paint_pet_badge(app);
        app->need_pet = false;
    }
    if(app->need_caption) {
        cads_desktop_paint_caption(app);
        app->need_caption = false;
    }
}

/* --- interaction -------------------------------------------------------------- */

static void cads_desktop_apply_mood(cads_desktop_t* app, uint32_t now_ms) {
    cads_leo_mood_t mood = cads_desktop_compute_mood(&app->leo, now_ms);
    if(mood != app->leo.mood) {
        app->leo.mood = mood;
        app->need_caption = true;
        cads_view_dirty_rect(&app->view, app->caption_rect);
    }
}

static void cads_desktop_pet(cads_desktop_t* app, uint32_t now_ms) {
    app->leo.interactions++;
    app->leo.last_interaction_ms = now_ms;

    app->pet_flash_active = true;
    app->pet_flash_until_ms = now_ms + CADS_DESKTOP_PET_FLASH_MS;
    app->need_pet = true;
    cads_view_dirty_rect(&app->view, app->pet_rect);

    cads_desktop_apply_mood(app, now_ms);
}

static bool cads_desktop_touch_in(cads_rect_t rect, const cads_input_event_t* event) {
    return event->x >= (uint16_t)rect.x && event->y >= (uint16_t)rect.y &&
           event->x < (uint16_t)(rect.x + rect.width) &&
           event->y < (uint16_t)(rect.y + rect.height);
}

static bool cads_desktop_input(const cads_input_event_t* event, void* context) {
    cads_desktop_t* app = (cads_desktop_t*)context;

    if(event->type == CadsInputRelease) {
        app->leo.last_interaction_ms = event->timestamp;
        if(event->key == CadsKeyOk) {
            return cads_view_dispatcher_push(app->dispatcher, CADS_VIEW_ID_MENU);
        }
        if(event->key == CadsKeyF1) {
            cads_desktop_pet(app, event->timestamp);
            return true;
        }
        return false;
    }

    if(event->type == CadsInputTouchUp && cads_desktop_touch_in(app->portrait_rect, event)) {
        cads_desktop_pet(app, event->timestamp);
        return true;
    }

    return false;
}

static void cads_desktop_enter(void* context) {
    cads_desktop_t* app = (cads_desktop_t*)context;
    uint32_t now = cads_hal_ticks_ms();

    cads_desktop_layout(app, cads_view_area(&app->view));
    app->need_full = true;
    app->leo.eyes_closed = false;

    cads_desktop_apply_mood(app, now);
    app->need_caption = false; /* the full redraw below already covers it */
    cads_desktop_schedule_blink(&app->leo, now);
}

/* --- public ------------------------------------------------------------------ */

void cads_desktop_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_desktop.dispatcher = dispatcher;
    s_desktop.leo.boot_ms = cads_hal_ticks_ms();
    s_desktop.leo.last_interaction_ms = s_desktop.leo.boot_ms;
    s_desktop.leo.mood = CADS_LEO_MOOD_SLEEPY;

    cads_view_init(&s_desktop.view, cads_desktop_draw, cads_desktop_input, &s_desktop);
    cads_view_set_lifecycle(&s_desktop.view, cads_desktop_enter, NULL);
    cads_view_set_title(&s_desktop.view, "CaDS Zero");
    cads_view_set_softkeys(
        &s_desktop.view, cads_desktop_keys,
        sizeof(cads_desktop_keys) / sizeof(cads_desktop_keys[0]));

    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_DESKTOP, &s_desktop.view);
}

void cads_desktop_tick(uint32_t now_ms) {
    if(s_desktop.dispatcher == NULL) return;
    if(cads_view_dispatcher_current_id(s_desktop.dispatcher) != CADS_VIEW_ID_DESKTOP) return;

    cads_leo_state_t* leo = &s_desktop.leo;

    if(leo->eyes_closed) {
        if((int32_t)(now_ms - leo->reopen_ms) >= 0) {
            leo->eyes_closed = false;
            cads_desktop_schedule_blink(leo, now_ms);
            s_desktop.need_eyes = true;
            cads_view_dirty_rect(&s_desktop.view, s_desktop.eyes_rect);
        }
    } else if((int32_t)(now_ms - leo->next_blink_ms) >= 0) {
        leo->eyes_closed = true;
        leo->reopen_ms = now_ms + cads_desktop_blink_close_ms(leo->mood);
        s_desktop.need_eyes = true;
        cads_view_dirty_rect(&s_desktop.view, s_desktop.eyes_rect);
    }

    if(s_desktop.pet_flash_active && (int32_t)(now_ms - s_desktop.pet_flash_until_ms) >= 0) {
        s_desktop.pet_flash_active = false;
        s_desktop.need_pet = true;
        cads_view_dirty_rect(&s_desktop.view, s_desktop.pet_rect);
    }

    cads_desktop_apply_mood(&s_desktop, now_ms);
}

void cads_desktop_notify_app_opened(void) {
    if(s_desktop.dispatcher == NULL) return;
    s_desktop.leo.apps_opened++;
    s_desktop.leo.last_interaction_ms = cads_hal_ticks_ms();
    /* The desktop is not the current view while another app is open, so
     * nothing needs to be dirtied here - cads_view_enter() forces a full
     * repaint with the updated level the next time the user sees Leo. */
}
