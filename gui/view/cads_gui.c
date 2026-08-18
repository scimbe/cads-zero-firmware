#include "cads_gui.h"

static cads_rect_t cads_gui_intersect(cads_rect_t a, cads_rect_t b) {
    int16_t x0 = a.x > b.x ? a.x : b.x;
    int16_t y0 = a.y > b.y ? a.y : b.y;
    int16_t x1 = (a.x + a.width) < (b.x + b.width) ? (int16_t)(a.x + a.width) :
                                                     (int16_t)(b.x + b.width);
    int16_t y1 = (a.y + a.height) < (b.y + b.height) ? (int16_t)(a.y + a.height) :
                                                       (int16_t)(b.y + b.height);
    cads_rect_t out = {x0, y0, (int16_t)(x1 > x0 ? x1 - x0 : 0), (int16_t)(y1 > y0 ? y1 - y0 : 0)};
    return out;
}

static bool cads_gui_contains(cads_rect_t rect, int16_t x, int16_t y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

static void cads_gui_layout(cads_gui_t* gui) {
    int16_t top = 0;
    int16_t bottom = CADS_CANVAS_HEIGHT;

    if(gui->statusbar != NULL) {
        cads_rect_t bar = {0, 0, CADS_CANVAS_WIDTH, CADS_STATUSBAR_HEIGHT};
        cads_statusbar_set_area(gui->statusbar, bar);
        top = CADS_STATUSBAR_HEIGHT;
    }
    if(gui->softkeys != NULL) {
        cads_rect_t strip = {
            0, (int16_t)(CADS_CANVAS_HEIGHT - CADS_SOFTKEYS_HEIGHT), CADS_CANVAS_WIDTH,
            CADS_SOFTKEYS_HEIGHT};
        cads_softkeys_set_area(gui->softkeys, strip);
        bottom = (int16_t)(CADS_CANVAS_HEIGHT - CADS_SOFTKEYS_HEIGHT);
    }

    gui->content.x = 0;
    gui->content.y = top;
    gui->content.width = CADS_CANVAS_WIDTH;
    gui->content.height = (int16_t)(bottom - top);
}

void cads_gui_init(
    cads_gui_t* gui,
    cads_view_dispatcher_t* dispatcher,
    cads_statusbar_t* statusbar,
    cads_softkeys_t* softkeys) {
    if(gui == NULL) return;

    gui->dispatcher = dispatcher;
    gui->statusbar = statusbar;
    gui->softkeys = softkeys;
    gui->background = CadsColorBackground;
    gui->seen_generation = 0xFFFFFFFFu; /* forces the first tick to adopt a view */
    gui->recompose = true;
    gui->started = false;
    cads_gui_layout(gui);
}

void cads_gui_set_background(cads_gui_t* gui, cads_color_t color) {
    if(gui == NULL || gui->background == color) return;
    gui->background = color;
    gui->recompose = true;
}

cads_rect_t cads_gui_content(const cads_gui_t* gui) {
    cads_rect_t empty = {0, 0, 0, 0};
    return (gui != NULL) ? gui->content : empty;
}

void cads_gui_invalidate(cads_gui_t* gui) {
    if(gui != NULL) gui->recompose = true;
}

/* --- input ---------------------------------------------------------------- */

static void cads_gui_route_key(cads_gui_t* gui, const cads_input_event_t* event) {
    /* The strip mirrors the physical key so both rails give the same feedback;
     * a cell already held by a finger keeps its own highlight. */
    if(gui->softkeys != NULL) {
        if(event->type == CadsInputPress) {
            cads_softkeys_highlight(gui->softkeys, event->key, true);
        } else if(event->type == CadsInputRelease) {
            cads_softkeys_highlight(gui->softkeys, event->key, false);
        }
    }
    (void)cads_view_dispatcher_input(gui->dispatcher, event);
}

void cads_gui_input(cads_gui_t* gui, const cads_input_event_t* event) {
    if(gui == NULL || event == NULL || gui->dispatcher == NULL) return;

    bool is_touch = (event->type == CadsInputTouchDown) || (event->type == CadsInputTouchMove) ||
                    (event->type == CadsInputTouchUp);

    if(!is_touch) {
        cads_gui_route_key(gui, event);
        return;
    }

    if(gui->softkeys != NULL) {
        cads_input_event_t synthesised;
        if(cads_softkeys_touch(gui->softkeys, event, &synthesised)) {
            if(synthesised.key != CadsKeyNone) {
                (void)cads_view_dispatcher_input(gui->dispatcher, &synthesised);
            }
            return;
        }
    }

    /* A touch on the status bar is swallowed rather than passed down: the bar
     * is an output surface, and letting it fall through would let a fingertip
     * aimed at the clock select the top row of the list behind it. */
    if(gui->statusbar != NULL &&
       cads_gui_contains(gui->statusbar->area, (int16_t)event->x, (int16_t)event->y)) {
        return;
    }

    (void)cads_view_dispatcher_input(gui->dispatcher, event);
}

/* --- frame ---------------------------------------------------------------- */

static void cads_gui_adopt_view(cads_gui_t* gui, cads_view_t* view) {
    cads_view_set_area(view, gui->content);

    if(gui->statusbar != NULL) {
        cads_statusbar_set_title(gui->statusbar, cads_view_title(view));
    }
    if(gui->softkeys != NULL) {
        size_t count = 0u;
        const cads_softkey_t* keys = cads_view_softkeys(view, &count);
        /* A view with no key table keeps whatever the previous one set, which
         * is what a transient overlay wants; a view with a table owns the
         * strip completely, blanks included. */
        if(keys != NULL) cads_softkeys_set(gui->softkeys, keys, count);
    }
}

uint32_t cads_gui_tick(cads_gui_t* gui, uint32_t now_ms) {
    if(gui == NULL || gui->dispatcher == NULL) return 0u;

    /* Repeats for a soft key held by a finger. Done before drawing so the
     * damage they cause lands in this frame rather than the next. */
    if(gui->softkeys != NULL) {
        cads_input_event_t repeat;
        while(cads_softkeys_tick(gui->softkeys, now_ms, &repeat)) {
            (void)cads_view_dispatcher_input(gui->dispatcher, &repeat);
        }
    }

    cads_view_t* view = cads_view_dispatcher_current(gui->dispatcher);
    uint32_t generation = cads_view_dispatcher_generation(gui->dispatcher);

    if(view != NULL && (generation != gui->seen_generation || !gui->started)) {
        gui->seen_generation = generation;
        gui->started = true;
        cads_gui_adopt_view(gui, view);
        gui->recompose = true;
    }

    if(gui->recompose) {
        gui->recompose = false;
        cads_canvas_fill_rect(
            gui->content.x, gui->content.y, gui->content.width, gui->content.height,
            gui->background);
        if(gui->statusbar != NULL) cads_statusbar_invalidate(gui->statusbar);
        if(gui->softkeys != NULL) cads_softkeys_invalidate(gui->softkeys);
        if(view != NULL) cads_view_dirty(view);
    }

    if(gui->statusbar != NULL) cads_statusbar_draw(gui->statusbar);
    if(gui->softkeys != NULL) cads_softkeys_draw(gui->softkeys);

    if(view != NULL) {
        cads_rect_t damage;
        if(cads_view_take_damage(view, &damage)) {
            cads_rect_t clip = cads_gui_intersect(damage, gui->content);
            if(clip.width > 0 && clip.height > 0) {
                /* Clipping to the damage is what makes a lazy draw callback
                 * correct: it may repaint the whole content area and still cost
                 * only the rectangle it declared. */
                cads_canvas_push_clip(clip);
                cads_view_render(view, gui->content);
                cads_canvas_pop_clip();
            }
        }
    }

    if(!cads_canvas_is_dirty()) return 0u;
    return cads_canvas_flush();
}

/* --- input service bridge ------------------------------------------------- */

static void cads_gui_input_trampoline(const cads_input_event_t* event, void* context) {
    cads_gui_t* gui = (cads_gui_t*)context;
    cads_gui_input(gui, event);
}

void cads_gui_attach_input(cads_gui_t* gui) {
    cads_input_set_callback(cads_gui_input_trampoline, gui);
}

void cads_gui_detach_input(void) {
    cads_input_set_callback(NULL, NULL);
}
