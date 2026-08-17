/*
 * CaDS Zero - input service.
 *
 * Turns the board's eight buttons and its touch panel into one event stream, so
 * an application never has to know which of them the user reached for.
 *
 * Hardware, confirmed against the board's official pin table and the
 * manufacturer's hardware test (see docs/HARDWARE.md):
 *
 *   S0..S7   PF0..PF7, ACTIVE LOW, internal pull-ups
 *   touch    XPT2046, single point, resistive
 *   USER     PC13 on the Nucleo, system escape, not an application key
 *
 * The eight buttons sit in a row under the display, so they are treated as a
 * soft-key strip whose labels are drawn directly above them, rather than being
 * bent into a D-pad shape. See docs/explanation/input-scheme.md.
 */

#ifndef CADS_INPUT_H
#define CADS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define CADS_BUTTON_COUNT 8

/** Logical keys. The default binding is positional: key n is button Sn. */
typedef enum {
    CadsKeyUp = 0,
    CadsKeyDown = 1,
    CadsKeyLeft = 2,
    CadsKeyRight = 3,
    CadsKeyOk = 4,
    CadsKeyBack = 5,
    CadsKeyF1 = 6,
    CadsKeyF2 = 7,
    CadsKeyNone = 0xFF,
} cads_key_t;

typedef enum {
    CadsInputPress,   /**< debounced press edge                              */
    CadsInputRelease, /**< release edge                                      */
    CadsInputRepeat,  /**< held past the repeat delay, then periodically     */
    CadsInputLong,    /**< held past the long-press threshold, emitted once  */
    CadsInputTouchDown,
    CadsInputTouchMove,
    CadsInputTouchUp,
} cads_input_type_t;

typedef struct {
    cads_input_type_t type;
    cads_key_t key;      /**< for button events                             */
    uint16_t x, y;       /**< for touch events                              */
    uint32_t timestamp;  /**< ms since boot                                 */
    uint32_t hold_ms;    /**< how long the key has been down, for repeats   */
} cads_input_event_t;

/* Timing. The repeat rate is chosen so a 30-row list scrolls end to end in
 * about two seconds of holding, which is brisk without overshooting. */
#define CADS_INPUT_DEBOUNCE_MS 20u
#define CADS_INPUT_REPEAT_DELAY_MS 400u
#define CADS_INPUT_REPEAT_PERIOD_MS 120u
#define CADS_INPUT_LONG_MS 800u

typedef void (*cads_input_callback_t)(const cads_input_event_t* event, void* context);

void cads_input_init(void);

/** Poll the hardware and emit any events. Call at 100 Hz or faster. */
void cads_input_tick(void);

/** Install the handler that receives events. One at a time; the GUI owns it. */
void cads_input_set_callback(cads_input_callback_t callback, void* context);

/** Current debounced state of a logical key. */
bool cads_input_is_down(cads_key_t key);

/** Bitmap of all pressed keys, bit n = key n. */
uint8_t cads_input_state(void);

/**
 * Rebind a logical key to a different physical button.
 *
 * Exists because the strip is labelled on screen: an app that wants OK under
 * the rightmost button can have it, and the label follows.
 */
void cads_input_bind(cads_key_t key, uint8_t button);

/** Human-readable name, for the soft-key strip's default labels. */
const char* cads_input_key_name(cads_key_t key);

#endif /* CADS_INPUT_H */
