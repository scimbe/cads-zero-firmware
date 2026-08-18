#include "cads_textbox.h"

#define CADS_TEXTBOX_PAD 6

static int16_t cads_text_advance(const cads_font_t* font, char ch) {
    unsigned char code = (unsigned char)ch;
    if(code < font->first || code > font->last) code = (unsigned char)' ';
    if(code < font->first || code > font->last) return 0;
    return (int16_t)font->glyphs[code - font->first].advance;
}

size_t cads_text_wrap(
    const cads_font_t* font,
    const char* text,
    int16_t width,
    cads_text_line_t* lines,
    size_t max) {
    if(font == NULL || text == NULL || lines == NULL || max == 0u || width <= 0) return 0u;

    size_t count = 0u;
    size_t start = 0u;
    size_t index = 0u;
    size_t last_space = 0u;
    bool have_space = false;
    int16_t pen = 0;

    while(text[index] != '\0' && count < max) {
        char ch = text[index];

        if(ch == '\n') {
            lines[count].offset = (uint16_t)start;
            lines[count].length = (uint16_t)(index - start);
            count++;
            index++;
            start = index;
            have_space = false;
            pen = 0;
            continue;
        }

        int16_t advance = cads_text_advance(font, ch);
        bool too_wide = (pen + advance > width) && (index > start);
        bool too_long = (index - start) + 1u >= CADS_TEXT_LINE_MAX;

        if(too_wide || too_long) {
            size_t end = index;
            size_t next = index;
            /* Prefer the last space, but only if it leaves something on the
             * line: a word wider than the box has to be broken somewhere. */
            if(have_space && last_space > start) {
                end = last_space;
                next = last_space + 1u;
            }
            lines[count].offset = (uint16_t)start;
            lines[count].length = (uint16_t)(end - start);
            count++;
            start = next;
            index = next;
            have_space = false;
            pen = 0;
            continue;
        }

        if(ch == ' ') {
            last_space = index;
            have_space = true;
        }
        pen = (int16_t)(pen + advance);
        index++;
    }

    if(count < max && text[index] == '\0' && index > start) {
        lines[count].offset = (uint16_t)start;
        lines[count].length = (uint16_t)(index - start);
        count++;
    }
    return count;
}

void cads_text_draw_line(
    int16_t x,
    int16_t y,
    const cads_font_t* font,
    const char* text,
    cads_text_line_t line,
    cads_color_t color) {
    if(font == NULL || text == NULL) return;

    char buffer[CADS_TEXT_LINE_MAX];
    size_t length = line.length;
    if(length >= sizeof(buffer)) length = sizeof(buffer) - 1u;
    for(size_t i = 0u; i < length; i++) buffer[i] = text[line.offset + i];
    buffer[length] = '\0';

    cads_canvas_draw_text(x, y, font, buffer, color);
}

/* --- the widget ----------------------------------------------------------- */

static void cads_textbox_rewrap(cads_textbox_t* box) {
    int16_t width = (int16_t)(box->area.width - 2 * CADS_TEXTBOX_PAD);
    box->line_count = cads_text_wrap(box->font, box->text, width, box->lines, box->line_capacity);

    box->row_height = (int16_t)box->font->line_height;
    if(box->row_height < 1) box->row_height = 1;
    box->visible = (box->area.height > 0) ? (size_t)(box->area.height / box->row_height) : 0u;

    if(box->line_count > box->visible && box->top > box->line_count - box->visible) {
        box->top = box->line_count - box->visible;
    } else if(box->line_count <= box->visible) {
        box->top = 0u;
    }
    box->dirty = true;
}

void cads_textbox_init(
    cads_textbox_t* box,
    const char* text,
    const cads_font_t* font,
    cads_text_line_t* lines,
    size_t line_capacity) {
    if(box == NULL) return;

    box->area.x = 0;
    box->area.y = 0;
    box->area.width = 0;
    box->area.height = 0;
    box->font = (font != NULL) ? font : &cads_font12;
    box->text = text;
    box->lines = lines;
    box->line_capacity = line_capacity;
    box->line_count = 0u;
    box->top = 0u;
    box->visible = 0u;
    box->row_height = (int16_t)box->font->line_height;
    box->dirty = true;
    box->dragging = false;
    box->drag_origin_y = 0;
    box->drag_origin_top = 0u;
}

void cads_textbox_set_area(cads_textbox_t* box, cads_rect_t area) {
    if(box == NULL) return;
    box->area = area;
    cads_textbox_rewrap(box);
}

void cads_textbox_set_text(cads_textbox_t* box, const char* text) {
    if(box == NULL) return;
    box->text = text;
    box->top = 0u;
    cads_textbox_rewrap(box);
}

void cads_textbox_scroll_to(cads_textbox_t* box, size_t line) {
    if(box == NULL) return;

    size_t max_top = (box->line_count > box->visible) ? box->line_count - box->visible : 0u;
    if(line > max_top) line = max_top;
    if(line == box->top) return;

    box->top = line;
    box->dirty = true;
}

size_t cads_textbox_line_count(const cads_textbox_t* box) {
    return (box != NULL) ? box->line_count : 0u;
}

bool cads_textbox_input(cads_textbox_t* box, const cads_input_event_t* event) {
    if(box == NULL || event == NULL) return false;

    switch(event->type) {
    case CadsInputPress:
    case CadsInputRepeat:
        if(event->key == CadsKeyUp) {
            cads_textbox_scroll_to(box, (box->top > 0u) ? box->top - 1u : 0u);
            return true;
        }
        if(event->key == CadsKeyDown) {
            cads_textbox_scroll_to(box, box->top + 1u);
            return true;
        }
        if(event->key == CadsKeyLeft) {
            size_t page = (box->visible > 1u) ? box->visible - 1u : 1u;
            cads_textbox_scroll_to(box, (box->top > page) ? box->top - page : 0u);
            return true;
        }
        if(event->key == CadsKeyRight) {
            size_t page = (box->visible > 1u) ? box->visible - 1u : 1u;
            cads_textbox_scroll_to(box, box->top + page);
            return true;
        }
        return false;

    case CadsInputTouchDown: {
        int16_t x = (int16_t)event->x;
        int16_t y = (int16_t)event->y;
        if(x < box->area.x || x >= box->area.x + box->area.width) return false;
        if(y < box->area.y || y >= box->area.y + box->area.height) return false;
        box->dragging = true;
        box->drag_origin_y = y;
        box->drag_origin_top = box->top;
        return true;
    }

    case CadsInputTouchMove: {
        if(!box->dragging || box->row_height <= 0) return false;
        int16_t delta = (int16_t)((int16_t)event->y - box->drag_origin_y);
        int16_t rows = (int16_t)(delta / box->row_height);
        size_t origin = box->drag_origin_top;
        if(rows > 0) {
            cads_textbox_scroll_to(box, (origin > (size_t)rows) ? origin - (size_t)rows : 0u);
        } else if(rows < 0) {
            cads_textbox_scroll_to(box, origin + (size_t)(-rows));
        }
        return true;
    }

    case CadsInputTouchUp:
        if(!box->dragging) return false;
        box->dragging = false;
        return true;

    default:
        return false;
    }
}

bool cads_textbox_is_dirty(const cads_textbox_t* box) {
    return box != NULL && box->dirty;
}

cads_rect_t cads_textbox_damage(const cads_textbox_t* box) {
    if(box != NULL) return box->area;
    cads_rect_t empty = {0, 0, 0, 0};
    return empty;
}

void cads_textbox_invalidate(cads_textbox_t* box) {
    if(box != NULL) box->dirty = true;
}

void cads_textbox_draw(cads_textbox_t* box) {
    if(box == NULL || !box->dirty || box->text == NULL) return;

    cads_canvas_fill_rect(
        box->area.x, box->area.y, box->area.width, box->area.height, CadsColorBackground);

    cads_canvas_push_clip(box->area);
    int16_t y = (int16_t)(box->area.y + 1);
    for(size_t i = box->top; i < box->line_count && i < box->top + box->visible; i++) {
        cads_text_draw_line(
            (int16_t)(box->area.x + CADS_TEXTBOX_PAD), y, box->font, box->text, box->lines[i],
            CadsColorWhite);
        y = (int16_t)(y + box->row_height);
    }
    cads_canvas_pop_clip();

    box->dirty = false;
}
