/*
 * CaDS Zero - input service implementation.
 *
 * Polled rather than interrupt driven, deliberately. The eight buttons are on
 * PF0..PF7, and EXTI shares one interrupt line per pin *number* across all
 * ports - EXTI0 is PA0 or PB0 or PF0, never several at once. Claiming EXTI0..7
 * for the buttons would take those lines away from every other port for the
 * rest of the project, to save a poll that costs one register read at 100 Hz.
 *
 * Debouncing is per button and time based: a transition is only believed once
 * the new level has held for CADS_INPUT_DEBOUNCE_MS. That is a state machine
 * rather than a delay, so a bouncing switch cannot stall the caller.
 */

#include "cads_input.h"

#include <stddef.h>

#include "cads_hal.h"

typedef struct {
    bool stable;         /**< debounced level, true = pressed                */
    bool candidate;      /**< level currently being timed                    */
    uint32_t since_ms;   /**< when the candidate level was first seen        */
    uint32_t pressed_at; /**< when the debounced press began                 */
    uint32_t next_repeat;
    bool long_sent;
} cads_button_t;

static cads_button_t cads_buttons[CADS_BUTTON_COUNT];

/*
 * Logical key -> physical button. Was the identity mapping (key n is Sn),
 * changed 2026-08-25 to the layout the user asked for after using the
 * default one live at the board: Up/Down swapped (S0=Down, S1=Up), OK
 * moved to S7 and Back to S6 (both were the two rightmost buttons under
 * the default mapping, matching a "primary actions live at the far end of
 * the row" preference), F1/F2 slid into the two slots that frees (S4, S5)
 * so every physical button still does something. cads_input_bind() at
 * runtime overrides any of this per key; this is only the boot default.
 */
/* Left/Right swapped 2026-08-26 (Left=S3, Right=S2): confirmed at the board
 * that S3 sits physically left of S2, so the prior Left=S2/Right=S3 moved the
 * cursor opposite to the pressed button. */
static uint8_t cads_key_binding[CADS_BUTTON_COUNT] = {1, 0, 3, 2, 7, 6, 4, 5};

static cads_input_callback_t cads_callback;
static void* cads_callback_context;

/* Touch, tracked so down/move/up can be synthesised from level samples. */
static bool cads_touch_down;
static uint16_t cads_touch_x, cads_touch_y;

static const char* const cads_key_names[CADS_BUTTON_COUNT] = {
    "Up", "Down", "Left", "Right", "OK", "Back", "F1", "F2"};

void cads_input_init(void) {
    uint32_t now = cads_hal_ticks_ms();
    for(uint32_t i = 0; i < CADS_BUTTON_COUNT; i++) {
        cads_buttons[i].stable = false;
        cads_buttons[i].candidate = false;
        cads_buttons[i].since_ms = now;
        cads_buttons[i].long_sent = false;
    }
    cads_touch_down = false;
}

void cads_input_set_callback(cads_input_callback_t callback, void* context) {
    cads_callback = callback;
    cads_callback_context = context;
}

void cads_input_bind(cads_key_t key, uint8_t button) {
    if((uint32_t)key < CADS_BUTTON_COUNT && button < CADS_BUTTON_COUNT) {
        cads_key_binding[key] = button;
    }
}

const char* cads_input_key_name(cads_key_t key) {
    return (uint32_t)key < CADS_BUTTON_COUNT ? cads_key_names[key] : "";
}

/** Reverse the binding: which logical key is this physical button? */
static cads_key_t cads_key_for_button(uint8_t button) {
    for(uint32_t key = 0; key < CADS_BUTTON_COUNT; key++) {
        if(cads_key_binding[key] == button) return (cads_key_t)key;
    }
    return CadsKeyNone;
}

bool cads_input_is_down(cads_key_t key) {
    if((uint32_t)key >= CADS_BUTTON_COUNT) return false;
    return cads_buttons[cads_key_binding[key]].stable;
}

uint8_t cads_input_state(void) {
    uint8_t state = 0u;
    for(uint32_t key = 0; key < CADS_BUTTON_COUNT; key++) {
        if(cads_buttons[cads_key_binding[key]].stable) state |= (uint8_t)(1u << key);
    }
    return state;
}

static void cads_emit(cads_input_type_t type, cads_key_t key, uint32_t now, uint32_t hold) {
    if(!cads_callback) return;
    cads_input_event_t event = {
        .type = type, .key = key, .x = 0, .y = 0, .timestamp = now, .hold_ms = hold};
    cads_callback(&event, cads_callback_context);
}

static void cads_emit_touch(cads_input_type_t type, uint16_t x, uint16_t y, uint32_t now) {
    if(!cads_callback) return;
    cads_input_event_t event = {
        .type = type, .key = CadsKeyNone, .x = x, .y = y, .timestamp = now, .hold_ms = 0};
    cads_callback(&event, cads_callback_context);
}

static void cads_tick_buttons(uint32_t now) {
    /* One register read for all eight. cads_hal_adapter_inputs() already
     * inverts the active-low wiring, so bit n set means Sn is pressed. */
    uint8_t raw = cads_hal_adapter_inputs();

    for(uint8_t index = 0; index < CADS_BUTTON_COUNT; index++) {
        cads_button_t* button = &cads_buttons[index];
        bool level = (raw & (1u << index)) != 0u;

        if(level != button->candidate) {
            /* Level changed: restart the debounce window rather than acting. */
            button->candidate = level;
            button->since_ms = now;
            continue;
        }

        if(level != button->stable && (now - button->since_ms) >= CADS_INPUT_DEBOUNCE_MS) {
            button->stable = level;
            cads_key_t key = cads_key_for_button(index);

            if(level) {
                button->pressed_at = now;
                button->next_repeat = now + CADS_INPUT_REPEAT_DELAY_MS;
                button->long_sent = false;
                cads_emit(CadsInputPress, key, now, 0u);
            } else {
                cads_emit(CadsInputRelease, key, now, now - button->pressed_at);
            }
            continue;
        }

        if(!button->stable) continue;

        uint32_t held = now - button->pressed_at;

        /* Long press fires once, and does not suppress repeats: a list still
         * scrolls while an app that also wants a long-press action gets it. */
        if(!button->long_sent && held >= CADS_INPUT_LONG_MS) {
            button->long_sent = true;
            cads_emit(CadsInputLong, cads_key_for_button(index), now, held);
        }

        if(now >= button->next_repeat) {
            button->next_repeat = now + CADS_INPUT_REPEAT_PERIOD_MS;
            cads_emit(CadsInputRepeat, cads_key_for_button(index), now, held);
        }
    }
}

static void cads_tick_touch(uint32_t now) {
    cads_touch_state_t touch;
    cads_hal_touch_read(&touch);

    if(touch.pressed) {
        if(!cads_touch_down) {
            cads_touch_down = true;
            cads_touch_x = touch.x;
            cads_touch_y = touch.y;
            cads_emit_touch(CadsInputTouchDown, touch.x, touch.y, now);
        } else if(touch.x != cads_touch_x || touch.y != cads_touch_y) {
            /* Resistive panels jitter by a pixel or two even under a steady
             * finger. Only report movement that a user could have intended. */
            int32_t dx = (int32_t)touch.x - (int32_t)cads_touch_x;
            int32_t dy = (int32_t)touch.y - (int32_t)cads_touch_y;
            if(dx * dx + dy * dy >= 9) {
                cads_touch_x = touch.x;
                cads_touch_y = touch.y;
                cads_emit_touch(CadsInputTouchMove, touch.x, touch.y, now);
            }
        }
    } else if(cads_touch_down) {
        cads_touch_down = false;
        cads_emit_touch(CadsInputTouchUp, cads_touch_x, cads_touch_y, now);
    }
}

void cads_input_tick(void) {
    uint32_t now = cads_hal_ticks_ms();
    cads_tick_buttons(now);
    cads_tick_touch(now);
}
