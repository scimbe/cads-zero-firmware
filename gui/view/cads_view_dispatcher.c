#include "cads_view_dispatcher.h"

static cads_view_t* cads_view_dispatcher_top(const cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL || dispatcher->depth == 0u) return NULL;
    return cads_view_dispatcher_find(dispatcher, dispatcher->stack[dispatcher->depth - 1u]);
}

void cads_view_dispatcher_init(
    cads_view_dispatcher_t* dispatcher,
    cads_view_entry_t* entries,
    size_t capacity,
    uint32_t* stack,
    size_t stack_depth) {
    if(dispatcher == NULL) return;

    dispatcher->entries = entries;
    dispatcher->capacity = (entries != NULL) ? capacity : 0u;
    dispatcher->count = 0u;
    dispatcher->stack = stack;
    dispatcher->stack_capacity = (stack != NULL) ? stack_depth : 0u;
    dispatcher->depth = 0u;
    dispatcher->generation = 0u;
    dispatcher->on_exhausted = NULL;
    dispatcher->context = NULL;
}

void cads_view_dispatcher_set_exhausted(
    cads_view_dispatcher_t* dispatcher, cads_view_exhausted_t callback, void* context) {
    if(dispatcher == NULL) return;
    dispatcher->on_exhausted = callback;
    dispatcher->context = context;
}

bool cads_view_dispatcher_add(cads_view_dispatcher_t* dispatcher, uint32_t id, cads_view_t* view) {
    if(dispatcher == NULL || view == NULL || id == CADS_VIEW_ID_NONE) return false;
    if(dispatcher->count >= dispatcher->capacity) return false;
    if(cads_view_dispatcher_find(dispatcher, id) != NULL) return false;

    dispatcher->entries[dispatcher->count].id = id;
    dispatcher->entries[dispatcher->count].view = view;
    dispatcher->count++;
    return true;
}

cads_view_t* cads_view_dispatcher_find(const cads_view_dispatcher_t* dispatcher, uint32_t id) {
    if(dispatcher == NULL || dispatcher->entries == NULL) return NULL;
    for(size_t i = 0u; i < dispatcher->count; i++) {
        if(dispatcher->entries[i].id == id) return dispatcher->entries[i].view;
    }
    return NULL;
}

static void cads_view_dispatcher_activate(
    cads_view_dispatcher_t* dispatcher, cads_view_t* leaving, cads_view_t* entering) {
    if(leaving == entering) return;
    if(leaving != NULL) cads_view_exit(leaving);
    if(entering != NULL) cads_view_enter(entering);
    dispatcher->generation++;
}

bool cads_view_dispatcher_switch_to(cads_view_dispatcher_t* dispatcher, uint32_t id) {
    if(dispatcher == NULL) return false;
    cads_view_t* target = cads_view_dispatcher_find(dispatcher, id);
    if(target == NULL) return false;

    cads_view_t* leaving = cads_view_dispatcher_top(dispatcher);
    if(dispatcher->depth == 0u) {
        if(dispatcher->stack_capacity == 0u) return false;
        dispatcher->depth = 1u;
    }
    dispatcher->stack[dispatcher->depth - 1u] = id;
    cads_view_dispatcher_activate(dispatcher, leaving, target);
    return true;
}

bool cads_view_dispatcher_push(cads_view_dispatcher_t* dispatcher, uint32_t id) {
    if(dispatcher == NULL) return false;
    cads_view_t* target = cads_view_dispatcher_find(dispatcher, id);
    if(target == NULL) return false;
    if(dispatcher->depth >= dispatcher->stack_capacity) return false;

    cads_view_t* leaving = cads_view_dispatcher_top(dispatcher);
    dispatcher->stack[dispatcher->depth++] = id;
    cads_view_dispatcher_activate(dispatcher, leaving, target);
    return true;
}

bool cads_view_dispatcher_pop(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL || dispatcher->depth <= 1u) return false;

    cads_view_t* leaving = cads_view_dispatcher_top(dispatcher);
    dispatcher->depth--;
    cads_view_dispatcher_activate(dispatcher, leaving, cads_view_dispatcher_top(dispatcher));
    return true;
}

void cads_view_dispatcher_pop_to_root(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL || dispatcher->depth <= 1u) return;

    cads_view_t* leaving = cads_view_dispatcher_top(dispatcher);
    dispatcher->depth = 1u;
    cads_view_dispatcher_activate(dispatcher, leaving, cads_view_dispatcher_top(dispatcher));
}

cads_view_t* cads_view_dispatcher_current(const cads_view_dispatcher_t* dispatcher) {
    return cads_view_dispatcher_top(dispatcher);
}

uint32_t cads_view_dispatcher_current_id(const cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL || dispatcher->depth == 0u) return CADS_VIEW_ID_NONE;
    return dispatcher->stack[dispatcher->depth - 1u];
}

size_t cads_view_dispatcher_depth(const cads_view_dispatcher_t* dispatcher) {
    return (dispatcher != NULL) ? dispatcher->depth : 0u;
}

bool cads_view_dispatcher_input(
    cads_view_dispatcher_t* dispatcher, const cads_input_event_t* event) {
    if(dispatcher == NULL || event == NULL) return false;

    cads_view_t* current = cads_view_dispatcher_top(dispatcher);
    if(current == NULL) return false;

    if(cads_view_handle_input(current, event)) return true;

    /* Back acts on Release, matching the click semantics of every other key, so
     * a user who presses Back and slides away still sees the press feedback but
     * does not navigate. */
    if(event->type == CadsInputRelease && event->key == CadsKeyBack) {
        if(cads_view_dispatcher_pop(dispatcher)) return true;
        if(dispatcher->on_exhausted != NULL) {
            dispatcher->on_exhausted(dispatcher->context);
            return true;
        }
    }
    return false;
}

uint32_t cads_view_dispatcher_generation(const cads_view_dispatcher_t* dispatcher) {
    return (dispatcher != NULL) ? dispatcher->generation : 0u;
}
