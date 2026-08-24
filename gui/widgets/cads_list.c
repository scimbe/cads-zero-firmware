#include "cads_list.h"

static int16_t cads_list_content_width(const cads_list_t* list) {
    int16_t width = list->area.width;
    if(list->count > list->visible) width = (int16_t)(width - CADS_LIST_SCROLLBAR_WIDTH);
    return width > 0 ? width : 0;
}

static void cads_list_mark_row(cads_list_t* list, size_t index) {
    if(index >= list->count) return;
    if(!list->dirty_rows) {
        list->dirty_rows = true;
        list->dirty_first = index;
        list->dirty_last = index;
        return;
    }
    if(index < list->dirty_first) list->dirty_first = index;
    if(index > list->dirty_last) list->dirty_last = index;
}

static void cads_list_recount_visible(cads_list_t* list) {
    int16_t height = list->font != NULL ? (int16_t)(list->font->line_height + CADS_LIST_ROW_PADDING) :
                                          1;
    if(height < 1) height = 1;
    list->row_height = height;
    list->visible = (list->area.height > 0) ? (size_t)(list->area.height / height) : 0u;
}

/** Bring `index` into the window with the least scrolling that achieves it,
 *  so a selection stepping down the list does not jump the window. */
static void cads_list_scroll_into_view(cads_list_t* list, size_t index) {
    if(list->visible == 0u) return;

    size_t top = list->top;
    if(index < top) {
        top = index;
    } else if(index >= top + list->visible) {
        top = index - list->visible + 1u;
    }
    if(list->count > list->visible) {
        size_t max_top = list->count - list->visible;
        if(top > max_top) top = max_top;
    } else {
        top = 0u;
    }
    if(top != list->top) {
        list->top = top;
        list->dirty_all = true;
    }
}

void cads_list_init(
    cads_list_t* list,
    size_t count,
    const cads_font_t* font,
    cads_list_row_draw_t row_draw,
    void* context) {
    if(list == NULL) return;

    list->area.x = 0;
    list->area.y = 0;
    list->area.width = 0;
    list->area.height = 0;
    list->font = (font != NULL) ? font : &cads_font16;
    list->count = count;
    list->selected = 0u;
    list->top = 0u;
    list->visible = 0u;
    list->wrap = false;
    list->background = CadsColorBackground;
    list->row_draw = row_draw;
    list->activate = NULL;
    list->context = context;
    list->dirty_all = true;
    list->dirty_rows = false;
    list->dirty_first = 0u;
    list->dirty_last = 0u;
    list->dragging = false;
    list->drag_moved = false;
    list->drag_origin_y = 0;
    list->drag_origin_top = 0u;
    cads_list_recount_visible(list);
}

void cads_list_set_activate(cads_list_t* list, cads_list_activate_t activate) {
    if(list != NULL) list->activate = activate;
}

void cads_list_set_background(cads_list_t* list, cads_color_t color) {
    if(list != NULL) list->background = color;
}

void cads_list_set_area(cads_list_t* list, cads_rect_t area) {
    if(list == NULL) return;
    list->area = area;
    cads_list_recount_visible(list);
    cads_list_scroll_into_view(list, list->selected);
    list->dirty_all = true;
}

void cads_list_set_count(cads_list_t* list, size_t count) {
    if(list == NULL) return;
    list->count = count;
    if(list->selected >= count) list->selected = (count > 0u) ? count - 1u : 0u;
    if(count <= list->visible) {
        list->top = 0u;
    } else if(list->top > count - list->visible) {
        list->top = count - list->visible;
    }
    list->dirty_all = true;
}

void cads_list_set_wrap(cads_list_t* list, bool wrap) {
    if(list != NULL) list->wrap = wrap;
}

size_t cads_list_selected(const cads_list_t* list) {
    return (list != NULL) ? list->selected : 0u;
}

size_t cads_list_count(const cads_list_t* list) {
    return (list != NULL) ? list->count : 0u;
}

void cads_list_set_selected(cads_list_t* list, size_t index) {
    if(list == NULL || list->count == 0u) return;
    if(index >= list->count) index = list->count - 1u;
    if(index == list->selected) return;

    size_t previous = list->selected;
    list->selected = index;
    cads_list_scroll_into_view(list, index);
    if(!list->dirty_all) {
        cads_list_mark_row(list, previous);
        cads_list_mark_row(list, index);
    }
}

static void cads_list_step(cads_list_t* list, int delta) {
    if(list->count == 0u) return;

    size_t index = list->selected;
    if(delta < 0) {
        if(index == 0u) {
            if(!list->wrap) return;
            index = list->count - 1u;
        } else {
            index--;
        }
    } else {
        if(index + 1u >= list->count) {
            if(!list->wrap) return;
            index = 0u;
        } else {
            index++;
        }
    }
    cads_list_set_selected(list, index);
}

static void cads_list_scroll_to(cads_list_t* list, size_t top) {
    if(list->count <= list->visible) top = 0u;
    else if(top > list->count - list->visible) top = list->count - list->visible;

    if(top != list->top) {
        list->top = top;
        list->dirty_all = true;
    }
}

static bool cads_list_row_at(const cads_list_t* list, int16_t y, size_t* index) {
    if(list->row_height <= 0 || list->visible == 0u) return false;
    if(y < list->area.y || y >= list->area.y + list->area.height) return false;

    size_t offset = (size_t)((y - list->area.y) / list->row_height);
    if(offset >= list->visible) return false;
    size_t absolute = list->top + offset;
    if(absolute >= list->count) return false;

    *index = absolute;
    return true;
}

bool cads_list_input(cads_list_t* list, const cads_input_event_t* event) {
    if(list == NULL || event == NULL) return false;

    switch(event->type) {
    case CadsInputPress:
    case CadsInputRepeat:
        if(event->key == CadsKeyUp) {
            cads_list_step(list, -1);
            return true;
        }
        if(event->key == CadsKeyDown) {
            cads_list_step(list, 1);
            return true;
        }
        /* Left and Right page through a long list; on a soft-key strip they are
         * otherwise idle, and paging is what a 200 row register dump needs. */
        if(event->key == CadsKeyLeft && list->visible > 0u) {
            size_t step = list->visible;
            cads_list_set_selected(list, (list->selected > step) ? list->selected - step : 0u);
            return true;
        }
        if(event->key == CadsKeyRight && list->visible > 0u) {
            cads_list_set_selected(list, list->selected + list->visible);
            return true;
        }
        return false;

    case CadsInputRelease:
        if(event->key == CadsKeyOk) {
            if(list->activate != NULL && list->count > 0u) {
                list->activate(list->selected, list->context);
            }
            return true;
        }
        return false;

    case CadsInputTouchDown: {
        int16_t x = (int16_t)event->x;
        int16_t y = (int16_t)event->y;
        if(x < list->area.x || x >= list->area.x + list->area.width) return false;
        if(y < list->area.y || y >= list->area.y + list->area.height) return false;

        list->dragging = true;
        list->drag_moved = false;
        list->drag_origin_y = y;
        list->drag_origin_top = list->top;
        return true;
    }

    case CadsInputTouchMove: {
        if(!list->dragging || list->row_height <= 0) return false;

        int16_t delta = (int16_t)((int16_t)event->y - list->drag_origin_y);
        int16_t rows = (int16_t)(delta / list->row_height);
        /* Half a row of slop before a press becomes a drag: a resistive panel
         * wanders by a pixel or two under a firm press, and a tap that scrolled
         * the list would make selection by touch impossible. */
        if(delta > list->row_height / 2 || delta < -(list->row_height / 2)) list->drag_moved = true;

        if(rows != 0) {
            size_t origin = list->drag_origin_top;
            size_t target;
            if(rows > 0) {
                target = (origin > (size_t)rows) ? origin - (size_t)rows : 0u;
            } else {
                target = origin + (size_t)(-rows);
            }
            cads_list_scroll_to(list, target);
        }
        return true;
    }

    case CadsInputTouchUp: {
        if(!list->dragging) return false;
        list->dragging = false;
        if(list->drag_moved) return true;

        size_t index;
        if(cads_list_row_at(list, (int16_t)event->y, &index)) {
            cads_list_set_selected(list, index);
            if(list->activate != NULL) list->activate(index, list->context);
        }
        return true;
    }

    default:
        return false;
    }
}

bool cads_list_is_dirty(const cads_list_t* list) {
    return list != NULL && (list->dirty_all || list->dirty_rows);
}

cads_rect_t cads_list_row_rect(const cads_list_t* list, size_t index) {
    cads_rect_t rect = {0, 0, 0, 0};
    if(list == NULL || index < list->top || index >= list->top + list->visible) return rect;

    rect.x = list->area.x;
    rect.y = (int16_t)(list->area.y + (int16_t)(index - list->top) * list->row_height);
    rect.width = cads_list_content_width(list);
    rect.height = list->row_height;
    return rect;
}

cads_rect_t cads_list_damage(const cads_list_t* list) {
    cads_rect_t box = {0, 0, 0, 0};
    if(list == NULL) return box;
    if(list->dirty_all || !list->dirty_rows) return list->area;

    size_t first = list->dirty_first;
    size_t last = list->dirty_last;
    if(first < list->top) first = list->top;
    if(last >= list->top + list->visible) {
        last = (list->visible > 0u) ? list->top + list->visible - 1u : list->top;
    }
    if(last < first) return box;

    cads_rect_t a = cads_list_row_rect(list, first);
    box.x = a.x;
    box.y = a.y;
    box.width = a.width;
    box.height = (int16_t)((int16_t)(last - first + 1u) * list->row_height);
    return box;
}

void cads_list_invalidate(cads_list_t* list) {
    if(list != NULL) list->dirty_all = true;
}

static void cads_list_draw_scrollbar(const cads_list_t* list) {
    if(list->count <= list->visible || list->visible == 0u) return;

    int16_t x = (int16_t)(list->area.x + list->area.width - CADS_LIST_SCROLLBAR_WIDTH);
    cads_canvas_fill_rect(
        x, list->area.y, CADS_LIST_SCROLLBAR_WIDTH, list->area.height, CadsColorGrayDark);

    int16_t track = list->area.height;
    int16_t thumb = (int16_t)((int32_t)track * (int32_t)list->visible / (int32_t)list->count);
    if(thumb < 8) thumb = 8;
    if(thumb > track) thumb = track;

    size_t span = list->count - list->visible;
    int16_t travel = (int16_t)(track - thumb);
    int16_t offset =
        (span > 0u) ? (int16_t)((int32_t)travel * (int32_t)list->top / (int32_t)span) : 0;

    cads_canvas_fill_rect(
        x, (int16_t)(list->area.y + offset), CADS_LIST_SCROLLBAR_WIDTH, thumb, CadsColorBrandLight);
}

void cads_list_draw(cads_list_t* list) {
    if(list == NULL || list->row_draw == NULL) return;
    if(!list->dirty_all && !list->dirty_rows) return;

    size_t first = list->top;
    size_t last = list->top + list->visible;

    if(!list->dirty_all) {
        if(list->dirty_first > first) first = list->dirty_first;
        if(list->dirty_last + 1u < last) last = list->dirty_last + 1u;
    }
    if(last > list->count) last = list->count;

    for(size_t index = first; index < last; index++) {
        cads_rect_t row = cads_list_row_rect(list, index);
        cads_canvas_push_clip(row);
        list->row_draw(index, row, index == list->selected, list->context);
        cads_canvas_pop_clip();
    }

    if(list->dirty_all) {
        /* The tail below the last row belongs to the list and nothing else
         * paints it, so an emptying list would leave stale rows behind. */
        int16_t used = (int16_t)((int16_t)(last - list->top) * list->row_height);
        if(used < list->area.height) {
            cads_canvas_fill_rect(
                list->area.x, (int16_t)(list->area.y + used), cads_list_content_width(list),
                (int16_t)(list->area.height - used), list->background);
        }
        cads_list_draw_scrollbar(list);
    }

    list->dirty_all = false;
    list->dirty_rows = false;
}
