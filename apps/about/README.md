# `apps/about` — the board descriptor, read and displayed

## What is it?

A single-view app that renders everything `cads_hal_board_info()` reports as
a scrolling, read-only page of text. It registers one view
(`CADS_VIEW_ID_ABOUT`, `0x0400`) into the app tree's `cads_view_dispatcher_t`
and shows up as the "About" row in `apps/menu` whenever `CADS_APP_ABOUT`
(CMake option, `ON` by default) is enabled. The page lists, in order: board
name, MCU name, CPU clock in MHz, display resolution and whether the panel
bus can be read back, measured pixel throughput and the full-redraw time it
implies, button count, touch/network/storage presence, usable flash and
DMA-capable RAM in KB, and a firmware build line from the compiler's
`__DATE__`/`__TIME__`. State is one static instance (`s_about`) with a fixed
768-byte text buffer and a 40-line wrap table — no allocation anywhere.

## Why is it shaped this way?

**Every field is read from the descriptor, not hard-coded, because that is
the entire point of the screen.** `core/cads_hal.h` is explicit that
`CADS_DISPLAY_WIDTH`/`HEIGHT` are compile-time maxima for sizing the static
framebuffer, and that everything else should ask `cads_hal_board_info()` at
run time instead of assuming them. This app is the proof that discipline is
actually followed: it prints `info->display_width`/`height`, never the
macros. The consequence is visible on the host build — `targets/sim/hal_sim.c`
reports `cpu_hz`, `flash_bytes`, `ram_bytes` and `display_pixels_per_second`
as honest zeros (a host binary has no clock divider or fixed RAM budget to
misreport), so `cads-zero-sim`'s About screen genuinely shows "CPU clock: 0
MHz" and "Flash: 0 KB usable" — not a bug, the simulator refusing to borrow
numbers from hardware it isn't.

**No `snprintf` anywhere in `cads_about_build_text()`.** The file's own
comment states why: linking it pulls in newlib's `_sbrk` heap-init stub,
which this project's linker script deliberately never provides — there is no
heap on this device (`docs/ROADMAP.md`, M0: `FreeRTOS integration, static
allocation only`). This is not theoretical for this file specifically:
`docs/ROADMAP.md`'s M6 log records that `desktop`/`settings`/`about`/`netinfo`
all shipped this exact `snprintf`/`_sbrk` link failure at once, the same
class of bug already caught in `apps/gpio`, and all four were fixed the same
way — `cads/toolbox/str.h` + `cads/toolbox/fmt.h`. `cads_about_str()` and
`cads_about_uint()` are the result: thin wrappers that track the write
position (`s_about_pos`) across calls and clamp it to `sizeof(s_about_text) -
1` on every call, because `cads_fmt_uint()`/`cads_str_append()` follow the
`snprintf` contract of returning the length the write *would* have taken
rather than how much it actually wrote.

**The throughput line guards against a divide-by-zero the simulator makes
real.** `full_screen_ms` is only computed `if(info->display_pixels_per_second
> 0u)` — because on the simulator that field is genuinely `0` (see above),
and dividing by it would be a fault, not a formatting quirk. On the ITSboard
the value is `342000` px/s, measured rather than derived
(`targets/itsboard/hal/hal_board_info.c`), which is what makes the "~448 ms
full redraw" line on real hardware a real number and not a guess.

**A scrolling `cads_textbox_t`, not a fixed list of rows.** The field count
here already runs past what a 320-pixel-tall panel shows at once, and it is
expected to grow (a future descriptor field appears here for free, nothing
else in this app has to change layout). `cads_textbox_input()`'s own contract
— Up/Down scroll one line, Left/Right scroll a page, OK and Back left alone —
is exactly why the soft-key row reads Up/Down/PgUp/PgDn/Back with no OK: this
screen has nothing to select, only to read.

**This is the one app whose `_init()` touches the HAL at registration time.**
`docs/ROADMAP.md` notes it directly while explaining why the host unit tests
needed a new fake: every other app's `_init()` only wires views and is
HAL-free, but `cads_about_init()` calls `cads_hal_board_info()` immediately
to build the text before the view is even pushed. Linking the real app tree
on the host therefore required `tests/unit/fake_hal.c` to grow a
`cads_hal_board_info()` stub — a fact about this app specifically, not the
app tree in general.

## How do I use it?

```c
#include "cads_about.h"
#include "cads_view_dispatcher.h"

void app_tree_init(cads_view_dispatcher_t* dispatcher) {
    cads_about_init(dispatcher); /* called by apps/menu; not usually needed directly */
}
```

## What are the limits?

- **Snapshot at init, never refreshed.** `cads_about_build_text()` runs once,
  inside `cads_about_init()`. There is no `cads_about_tick()` (unlike
  `apps/desktop`/`apps/gpio`/`apps/game`); the board descriptor doesn't change
  at runtime, so this is intentional, but it does mean the firmware build
  timestamp and every other line are fixed the instant the view registers.
- **Silent truncation past 768 bytes or 40 wrapped lines.** `s_about_pos` is
  clamped, not checked, when the buffer fills — unlike `cads_ring`'s
  documented "drops the newest byte and counts it," an overlong
  `board_name`/`mcu_name` here just gets cut with no counter and no signal
  that it happened. `cads_text_wrap()`'s return value (equal to `max` means
  truncated) is likewise never inspected by this app.
- **No firmware version string.** `cads_board_info_t` carries no version
  field; "Firmware built" is a compiler `__DATE__ __TIME__` stamp, not a
  semantic version — there is nothing else in this codebase to display in its
  place yet.
- **Read-only.** No OK action, no editing, no way to trigger a rescan or
  refresh from this screen — scrolling is the only input it consumes.
- **Single static instance.** `s_about` and `s_about_text` are file-scope
  statics; `cads_about_init()` is meant to be called once, by `apps/menu`.
