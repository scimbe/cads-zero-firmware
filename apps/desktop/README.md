# `apps/desktop` — the home screen and Leo's residence

## What is it?

The navigation root (`CADS_VIEW_ID_DESKTOP`, `0x0100`). Draws Leo the lion -
this project's mascot, replacing the dolphin the device this firmware takes
its cues from ships with - and a caption naming his mood and level. `OK` opens
the main menu (`apps/menu`); `F1`, or tapping Leo's portrait, pets him.

## Why is it shaped this way?

**Leo's state is small and honest, not a simulation.** Two raw counters
(`apps_opened`, `interactions`) plus uptime feed one `level` number; four
thresholds turn that into a mood (dozing / content / cheerful / wired); three
minutes without an interaction forces dozing regardless of level. That is the
whole machine - it is meant to visibly respond to real use, not to model a
lion.

**The state is not persisted yet.** `modules/storage` is being built
concurrently and this app does not depend on it. `cads_leo_state_t` in
`cads_desktop.c` is a plain static struct with a comment marking where to load
it after `cads_hal_init()` and save it on change once that module lands - Leo
currently forgets everything on reboot.

**The blink is not Leo's bitmap changing.** `cads_canvas_draw_bitmap4()` draws
a buffer that *is* the image at the given width; it has no source stride, so
it cannot blit a cropped rectangle out of the middle of `cads_leo` (160×151).
Redrawing the whole portrait every blink would cost roughly 70 ms at this
panel's throughput - measured, not estimated
(`docs/reference/measurements.md`) - which is not an affordable price for a
cosmetic animation. The blink is instead a pair of eye shapes drawn with plain
canvas primitives in one small fixed rectangle *on top of* the static
portrait; each state (open/closed) is fully opaque over that rectangle, so
there is nothing to erase between them. Total redraw per blink: one ~48×16
rectangle, well inside the 40×30 budget the constraint allows.

**The eye rectangle's position is an estimate.** It was placed from the
portrait's proportions, not measured against the actual pixels - see
`CADS_DESKTOP_EYES_OFFSET_X/Y` in `cads_desktop.c` and nudge them if the
overlay does not land on Leo's eyes on the real panel.

**`cads_desktop_tick()` exists because the view framework has no periodic
callback.** `cads_view_t` (`gui/view/cads_view.h`) is draw/input/enter/exit
only - nothing before the desktop needed the passage of time on its own.
Blinking does, so this app adds a `_tick(now_ms)` function in the same style
as the existing `cads_input_tick()` / `cads_gui_tick()`, meant to be called
once per main loop iteration. It is a no-op whenever the desktop is not the
visible view.

## How do I use it?

```c
cads_desktop_init(&dispatcher);
/* ... register every other app, see apps/menu/README.md ... */
cads_view_dispatcher_push(&dispatcher, CADS_VIEW_ID_DESKTOP);

for(;;) {
    cads_input_tick();
    cads_desktop_tick(cads_hal_ticks_ms());
    cads_gui_tick(&gui, cads_hal_ticks_ms());
}
```

`apps/menu` calls `cads_desktop_notify_app_opened()` when it launches
something, which is how Leo's level reflects real use rather than uptime
alone.

## What are the limits?

- No persistence (see above) - Leo resets every boot.
- The mood/level formula is a placeholder tuned for "looks alive in a demo",
  not a designed progression system.
- The eye overlay is not Leo's actual eyes; it is drawn on top of them in an
  estimated position.
