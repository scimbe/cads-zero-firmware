#include "cads_view.h"

void cads_view_init(
    cads_view_t* view, cads_view_draw_t draw, cads_view_input_t input, void* context) {
    if(view == NULL) return;

    view->draw = draw;
    view->input = input;
    view->enter = NULL;
    view->exit = NULL;
    view->context = context;
    view->title = NULL;
    view->keys = NULL;
    view->key_count = 0u;
    view->area.x = 0;
    view->area.y = 0;
    view->area.width = 0;
    view->area.height = 0;
    view->damage = view->area;
    view->damage_valid = false;
}

void cads_view_set_lifecycle(cads_view_t* view, cads_view_enter_t enter, cads_view_exit_t exit) {
    if(view == NULL) return;
    view->enter = enter;
    view->exit = exit;
}

void cads_view_set_softkeys(cads_view_t* view, const cads_softkey_t* keys, size_t count) {
    if(view == NULL) return;
    view->keys = keys;
    view->key_count = count;
}

void cads_view_set_title(cads_view_t* view, const char* title) {
    if(view != NULL) view->title = title;
}

const char* cads_view_title(const cads_view_t* view) {
    return (view != NULL) ? view->title : NULL;
}

cads_rect_t cads_view_area(const cads_view_t* view) {
    cads_rect_t empty = {0, 0, 0, 0};
    return (view != NULL) ? view->area : empty;
}

void cads_view_dirty(cads_view_t* view) {
    if(view == NULL) return;
    view->damage = view->area;
    view->damage_valid = true;
}

void cads_view_dirty_rect(cads_view_t* view, cads_rect_t rect) {
    if(view == NULL || rect.width <= 0 || rect.height <= 0) return;

    if(!view->damage_valid) {
        view->damage = rect;
        view->damage_valid = true;
        return;
    }

    int16_t x0 = view->damage.x < rect.x ? view->damage.x : rect.x;
    int16_t y0 = view->damage.y < rect.y ? view->damage.y : rect.y;
    int16_t x1 = (view->damage.x + view->damage.width) > (rect.x + rect.width) ?
                     (int16_t)(view->damage.x + view->damage.width) :
                     (int16_t)(rect.x + rect.width);
    int16_t y1 = (view->damage.y + view->damage.height) > (rect.y + rect.height) ?
                     (int16_t)(view->damage.y + view->damage.height) :
                     (int16_t)(rect.y + rect.height);

    view->damage.x = x0;
    view->damage.y = y0;
    view->damage.width = (int16_t)(x1 - x0);
    view->damage.height = (int16_t)(y1 - y0);
}

bool cads_view_is_dirty(const cads_view_t* view) {
    return view != NULL && view->damage_valid;
}

void cads_view_set_area(cads_view_t* view, cads_rect_t area) {
    if(view == NULL) return;
    view->area = area;
    cads_view_dirty(view);
}

bool cads_view_damage(const cads_view_t* view, cads_rect_t* out) {
    if(view == NULL || out == NULL || !view->damage_valid) return false;
    *out = view->damage;
    return true;
}

void cads_view_clear_damage(cads_view_t* view) {
    if(view != NULL) view->damage_valid = false;
}

void cads_view_enter(cads_view_t* view) {
    if(view == NULL) return;
    /* Dirty before the callback, so a view that narrows the damage inside
     * enter() is not overruled by the framework afterwards. */
    cads_view_dirty(view);
    if(view->enter != NULL) view->enter(view->context);
}

void cads_view_exit(cads_view_t* view) {
    if(view == NULL) return;
    if(view->exit != NULL) view->exit(view->context);
    view->damage_valid = false;
}

void cads_view_render(cads_view_t* view, cads_rect_t area) {
    if(view == NULL || view->draw == NULL) return;
    view->draw(area, view->context);
}

bool cads_view_handle_input(cads_view_t* view, const cads_input_event_t* event) {
    if(view == NULL || view->input == NULL || event == NULL) return false;
    return view->input(event, view->context);
}

const cads_softkey_t* cads_view_softkeys(const cads_view_t* view, size_t* count) {
    if(view == NULL) {
        if(count != NULL) *count = 0u;
        return NULL;
    }
    if(count != NULL) *count = view->key_count;
    return view->keys;
}
