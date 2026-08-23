# `apps/gpio` — live OUT0..15 / IN0..7 / INT0..5 on the panel

## What is it?

The on-panel counterpart to the ITS adapter's 30 raw I/O lines: a
`cads_view` (`CADS_VIEW_ID_GPIO`, `0x0500`) that registers with the menu
(`cads_gpio_init(dispatcher)`, called from `apps/menu/cads_menu_app.c` when
`CADS_APP_GPIO` is `ON`) and shows the whole adapter at once. Two 36 px
strips across the top display IN0..7 and INT0..5 as eight and six live
cells, each lit (`CadsColorAccent`) when its line is asserted and dark
(`CadsColorGrayDark`) otherwise. Below the strips, a `cads_list` of 16 rows
— one per OUT0..15 — shows each output's current on/off state; Up/Down move
the selection, OK toggles the selected output, and the F1 soft key
(`"All off"`) clears all sixteen at once. `cads_gpio_tick()` polls the
adapter's input pins at a fixed rate and repaints only the cells that
changed; it must be pumped once per main-loop iteration for the IN/INT
strips to update at all (see below).

## Why is it shaped this way?

**`out_state` is a shadow because the HAL gives outputs no getter.**
`cads_hal_adapter_outputs()` (`core/cads_hal.h`) takes the whole 16-bit word
in one call and has no matching read-back function. Toggling OUT7 therefore
means reading this app's own `out_state` field, flipping one bit, and
writing the whole word back (`cads_gpio_out_activate()`) — the same shadow
pattern `apps/settings` uses for brightness and SPI clock, and for the same
reason: it is wrong the moment anything else drives PD/PE without going
through this app (the bring-up console's `o` command does exactly that).
IN0..7 and INT0..5 need no such shadow — they are read-only and
`cads_hal_adapter_inputs()` / `cads_hal_adapter_interrupts()` can simply be
polled.

**`cads_gpio_tick()` exists because IN/INT never generate an input event.**
`services/input` turns physical key presses into `cads_input_event_t`s, but
IN0..7 and INT0..5 are external signals wired straight to GPIOF/GPIOG — they
are not keys, so nothing in the input service ever calls this view's
`draw()` on their behalf. Someone has to poll the pins, which is what
`cads_gpio_tick()` does: it checks `cads_view_dispatcher_current_id()` first
and returns immediately unless GPIO is the visible view, then polls at
`CADS_GPIO_POLL_PERIOD_MS` (50 ms) using a `next_poll_ms` deadline rather
than a divide-down counter. The same pattern as `cads_desktop_tick()` —
declared in the header so a caller pumps it every loop iteration and pays
almost nothing when this view is not on screen.

**IN0..7 and INT0..5 read active-*high* here despite being active-*low* on
the wire.** `cads_hal_adapter_inputs()`'s own doc comment
(`core/cads_hal.h`) says "Bit n = INn", and `hal_io.c` inverts the raw `IDR`
read before returning it ("Active low on the wire; report active high so
callers read naturally"). `cads_gpio_paint_cell()` treats a set bit as
`active` and paints it in the accent color — that lit cell means the signal
is genuinely asserted (a button held, an external line driven true), not
that the MCU pin is physically high.

**Nothing here can drive PF0..7 or PG0..5, by construction, not by
discipline.** Per `docs/SAFETY.md` §3 and the adapter table in
`docs/HARDWARE.md` §4, PF0..7 (IN0..7) and PG0..5 (INT0..5) are inputs the
adapter itself may be actively driving; configuring them as outputs risks
two push-pull drivers fighting over one net. `cads_hal_adapter_outputs()`
only ever reaches PD0..7/PE0..7 (OUT0..15) — there is no HAL entry point
this app could call even if it wanted to touch PF/PG, so the safety
constraint holds without a runtime check.

**The OUT rows reuse `cads_list`; the IN/INT strips do not.** Sixteen
selectable, scrollable, toggle-on-activate rows are exactly what
`cads_list` already provides (Up/Down/OK, per-row dirty tracking, touch), so
`cads_gpio_out_row_draw()` / `cads_gpio_out_activate()` just plug into it —
the same widget `apps/menu` and `apps/settings` use. IN0..7 and INT0..5 are
a different shape: a fixed bank of same-row indicator lamps with nothing to
select or scroll, so they get their own small strip layout
(`cads_gpio_layout()`, `cads_gpio_cell_rect()`) instead of being forced into
a list with a selection that means nothing.

**`draw()` distinguishes a first full paint from incremental cell repaints.**
On `cads_gpio_enter()`, `need_strips` is set and the whole IN/INT area is
drawn once; every tick after that repaints only the cells whose bit changed
(`in_dirty` / `int_dirty` bitmasks), and `cads_gpio_tick()` unions the
changed cells' rectangles into one `cads_view_dirty_rect()` call. A full
repaint on this panel costs on the order of the 448 ms figure in
`docs/explanation/dirty-rectangles.md`; polling at 50 Hz and repainting
everything every time would make the view visibly lag behind the wires.

## How do I use it?

```c
#include "cads_gpio.h"
#include "cads_hal.h"
#include "cads_view_dispatcher.h"

#define VIEW_CAPACITY 1u
#define STACK_DEPTH   1u

static cads_view_entry_t entries[VIEW_CAPACITY];
static uint32_t stack[STACK_DEPTH];
static cads_view_dispatcher_t dispatcher;

void gpio_screen_run(uint32_t seconds) {
    cads_view_dispatcher_init(&dispatcher, entries, VIEW_CAPACITY, stack, STACK_DEPTH);
    cads_view_dispatcher_set_area(
        &dispatcher, (cads_rect_t){0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT});

    cads_gpio_init(&dispatcher); /* normally apps/menu does this */
    cads_view_dispatcher_switch_to(&dispatcher, CADS_VIEW_ID_GPIO);

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        /* Polls IN0..7/INT0..5 and marks changed cells dirty; a no-op
         * whenever GPIO is not the current view. A real app also calls
         * cads_gui_tick() (gui/cads_gui.h) here to flush pixels and route
         * touch/key input - omitted for brevity. */
        cads_gpio_tick(cads_hal_ticks_ms());
        cads_hal_delay_ms(10u);
    }
}
```

## What are the limits?

- **The output shadow can go stale.** `out_state` is this app's own record
  of which OUT bits are set, seeded to all-off at init and never read back
  from hardware (there is no getter to read back from). Anything else that
  calls `cads_hal_adapter_outputs()` — the bring-up console's `o` command,
  for instance — desyncs the shadow from the real pins until this app's own
  toggle or "All off" corrects it.
- **No numeric entry.** Outputs can only be flipped one bit at a time from
  the list (OK toggles the selected row) or all cleared at once (F1); there
  is no way to set OUT0..15 to an arbitrary 16-bit value from this screen the
  way the bring-up console's `o <hex>` command can.
- **IN0..7 and INT0..5 are read-only displays of the wire.** There is no
  soft-key or touch action that simulates a signal for testing; a value on
  screen only changes when the physical line does, and there is no debounce
  — `cads_gpio_tick()` redraws on every raw bit transition it observes, so a
  bouncy contact on IN/INT would flicker the corresponding cell.
- **Polling is fixed at 50 ms and only while this view is current.**
  `CADS_GPIO_POLL_PERIOD_MS` is a compile-time constant, not configurable at
  runtime, and `cads_gpio_tick()` does nothing at all when another view is on
  screen — an IN/INT transition that happens while the user is elsewhere is
  simply missed, not queued.
- **One static instance.** `cads_gpio_init()` wires a single file-scope
  `s_gpio` into the dispatcher; there is no way to run two GPIO views, or
  reset one independently of the process, from this API.
