# `apps/settings` — brightness, SPI clock, calibration, factory reset

## What is it?

A `cads_menu` of four rows (`CADS_VIEW_ID_SETTINGS`, `0x0300`) plus a second,
reusable confirm/info view (`CADS_VIEW_ID_SETTINGS_CONFIRM`, `0x0301`) pushed
for the two rows that need a modal answer.

- **Brightness** cycles 25/50/75/100% and calls `cads_hal_display_backlight()`.
- **SPI clock** toggles the safe `/16` divider and the faster `/8` one via
  `cads_hal_display_set_fast_clock()`. A status line under the list states the
  Ethernet consequence of whichever is active.
- **Touch calibration** opens an info dialog explaining that there is nothing
  to configure yet - an entry point, not a working flow.
- **Factory reset** opens a Yes/No confirmation and, on Yes, resets brightness
  and the SPI clock to their defaults.

## Why is it shaped this way?

**The HAL has no getters for backlight or SPI clock.**
`cads_hal_display_backlight()` and `cads_hal_display_set_fast_clock()`
(`core/cads_hal.h`) are setters only. "Read current values from the HAL
rather than assuming defaults" is therefore not fully achievable today: this
app keeps its own shadow (`cads_settings_shadow_t`), seeded from the values
`apps/bringup/bringup.c` itself leaves the hardware in after its self-test
(80% backlight, the safe `/16` clock), and treats every change *it* makes as
the new truth from then on. A `cads_hal_display_backlight_get()` and a
`cads_hal_display_is_fast_clock()` would close this gap properly; until they
exist, this shadow is the best available substitute, and it is wrong the
moment something else changes the hardware without going through this app.

**The confirm dialog is a separate pushed view, not a `cads_dialog` nested
inside the settings view.** A nested dialog can draw its own Yes/No/OK
buttons correctly, but the soft-key *strip* only gets re-applied by the
compositor when the current view changes (`cads_gui_adopt_view()`, driven by
the dispatcher's generation counter) - a view has no supported way to update
its own key labels while it stays current. Pushing the dialog as its own view
makes the view-change itself carry the new labels
(`cads_dialog_softkeys()` into `cads_view_set_softkeys()` before the push), so
the physical strip and the on-screen buttons always agree. This is a real
limitation of the current framework, not specific to this app - see the
maintainer report for the general case.

**SPI clock's status line explains the trade-off it makes.** Per
`docs/explanation/pa7-conflict.md`, the display and Ethernet time-share PA7;
the SPI divider governs both the panel's redraw speed *and* the longest
Ethernet blackout per redraw band (22.5 ms at `/16`, 11.5 ms at `/8`). Toggling
the divider without saying so would be changing a network property from a
display setting with no explanation.

## How do I use it?

```c
cads_settings_init(&dispatcher); /* called by apps/menu; not usually direct */
```

## What are the limits?

- Shadow state only - see above. Also not persisted (`modules/storage` is not
  wired in yet); a reboot returns to the seeded 80%/`safe` values regardless
  of what the user last chose.
- Touch calibration is an entry point, not an implementation - there is no
  calibration math anywhere in this app or in `core/cads_hal.h` to hang one
  off yet.
- Factory reset resets this app's own shadow values only; it does not touch
  anything persisted, because nothing is persisted yet.
