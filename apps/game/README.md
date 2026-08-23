# `apps/game` — Leo's Reflex Test

## What is it?

A single view (`CADS_VIEW_ID_GAME`, `0x0800`) registered as the "Reflex Test"
row in `apps/menu`. It runs a five-state reaction-time test
(`cads_game_state_t`): **IDLE** waits for OK; **ARMED** holds off for a
random 800–2800 ms and then flips itself to **GO** with no key involved;
**GO** waits for the OK release and turns the elapsed
`cads_hal_ticks_ms()` delta into the round's `reaction_ms`; **TOO_SOON** is
a false start (OK pressed while still ARMED); **RESULT** shows that round's
time and `best_ms`, the lowest reaction time recorded so far this session.
The soft-key strip is fixed for the whole app - OK ("Go") starts or repeats
a round, and every state leaves `CadsKeyBack` unconsumed so the
dispatcher's default pop closes the game the same way it closes anything
else.

## Why is it shaped this way?

**`cads_game_tick()` exists because a view cannot wake itself up.**
`cads_view_t` is draw/input/enter/exit and nothing else - a menu row only
changes because a key moved it, but this game's ARMED-to-GO transition has
to happen on its own, with no key pressed at all, because that unpredictable
wait is the entire test. `apps/desktop`'s blink clock hit the identical
problem first and solved it with `cads_desktop_tick()`; this app reuses that
pattern rather than inventing a new one, and `apps/bringup/explorer_app_demo.c`
calls `cads_desktop_tick()`, `cads_gpio_tick()` and `cads_game_tick()` back to
back in the same main-loop iteration. The header spells out the cost of
calling it unconditionally: a no-op (guarded by an internal ready flag) until
`cads_game_init()` has run, and "a few comparisons" the rest of the time.

**The PRNG is local to this file, not a `modules/toolbox` module.** Nothing
else in the tree needed randomness before this game did, so there was no
`cads/toolbox/rng.h` to reach for; the xorshift32 in `cads_game.c` is, per
its own comment, meant to be promoted to the toolbox "if a second caller
ever needs one" rather than pre-built as shared infrastructure for a
hypothetical one. It is seeded once, in `cads_game_init()`, from
`cads_hal_ticks_us()` - free-running before any input has happened, so its
low bits are as good a seed as the firmware has at that point - with a
fixed fallback constant (`0x9E3779B9`) for the one state xorshift32 can
never recover from: an all-zero seed.

**The 800–2800 ms armed window (`CADS_GAME_ARMED_MIN_MS` = 800,
`CADS_GAME_ARMED_SPAN_MS` = 2000) is a deliberate range, not a placeholder.**
Long enough that a false start is a genuine mistake rather than something a
player can beat by counting a fixed interval; short enough that waiting for
the signal doesn't itself feel like the point of the app. Both constants are
named and commented in the source rather than left as bare numbers.

**Reaction and best-time text is built with `cads/toolbox/fmt.h` and
`cads/toolbox/str.h` into fixed on-stack buffers** (`cads_fmt_uint()`,
`cads_str_append()`), not `snprintf` - the same no-`malloc`, no-libc
formatting discipline `modules/toolbox`'s own README documents for the rest
of this codebase, applied here rather than reinvented.

**The app is a CMake option**, `CADS_APP_GAME` (default `ON`), which turns
into a `CADS_APP_GAME_ENABLED` compile definition on `cads_flags` - the same
build-time-optional mechanism `apps/settings`, `apps/about`, `apps/gpio`,
`apps/netinfo` and `apps/filebrowser` all use, added project-wide after RAM
pressure during bring-up, not something specific to this app. `apps/menu`
and every direct caller of `cads_game_tick()`/`cads_game_init()` guard the
call with `#ifdef CADS_APP_GAME_ENABLED` so a build with the option off
still links.

## How do I use it?

```c
#include "cads_game.h"
#include "cads_hal.h"
#include "cads_view_dispatcher.h"

static cads_view_dispatcher_t dispatcher;
static cads_view_entry_t entries[16];
static uint32_t nav_stack[4];

void app_setup(void) {
    cads_view_dispatcher_init(&dispatcher, entries, 16, nav_stack, 4);
    cads_game_init(&dispatcher); /* registers CADS_VIEW_ID_GAME (0x0800) */
}

void app_main_loop(void) {
    for(;;) {
        uint32_t now = cads_hal_ticks_ms();
        /* Called once per iteration, alongside cads_desktop_tick()/
         * cads_gpio_tick() - lets an ARMED round flip to GO on its own. */
        cads_game_tick(now);
        cads_hal_delay_ms(10u);
    }
}
```

`apps/menu` already does exactly this (`cads_game_init(dispatcher)` at
registration time, behind `CADS_APP_GAME_ENABLED`); the row it adds is
titled "Reflex Test" and pushes `CADS_VIEW_ID_GAME`.

## What are the limits?

- **One static instance.** `s_game` and its ready flag are file-static, like
  every other app view in this tree - there is no way to run two independent
  game views, and `cads_game_init()` is not guarded against being called
  twice.
- **`best_ms` is RAM-only.** Nothing here touches `modules/storage`; a
  reboot resets it to "no round finished yet" (0), the same situation
  `apps/settings` documents for its own shadow state.
- **One metric, one cue.** It measures raw reaction time to a single fixed
  "GO!" text cue at a single screen position - no difficulty levels, no
  configurable delay window, no per-round history beyond the single
  session-best, no average over multiple rounds.
- **Not a security-grade RNG.** The armed delay comes from a 32-bit
  xorshift, good enough that a player cannot count out an 800–2800 ms
  window but not intended for anything where unpredictability has to
  resist an adversary.
- **Input is OK-release only.** Every other key (including OK's own press
  edge) and every other input source the platform has (touch, GPIO) are
  irrelevant to this app; only `CadsInputRelease` + `CadsKeyOk` does
  anything, everything else falls through unconsumed.
