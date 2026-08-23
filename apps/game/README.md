# `apps/game` — Leo's Arcade

## What is it?

A select screen (`CADS_VIEW_ID_GAME`, `0x0800`) over four cartridges, each
its own `cads_view_t` pushed onto the dispatcher when chosen: **Reflex
Test** (`0x0801`, the original single-game app - a reaction-time test),
**Snake** (`0x0802`), **Breakout** (`0x0803`) and **Dodger** (`0x0804`, an
auto-scrolling gap-dodging game). `apps/menu` still registers one row,
titled "Reflex Test" for the row label but pushing the select screen's
`CADS_VIEW_ID_GAME`, not any one cartridge directly. Up/Down move the
highlight on the select screen, OK pushes the highlighted cartridge, and
Back - left unconsumed everywhere, the same as every other view in this
tree - falls through to the dispatcher's own default pop, so it always
returns to whatever is one level up (the select screen from inside a
cartridge, the menu from the select screen) without this app writing any
navigation code for it.

Each cartridge's actual rules - grid movement and collision for Snake,
ball/paddle/brick physics for Breakout, obstacle scroll and gap collision
for Dodger - live in `modules/toolbox/{snake,breakout,dodger}.h` and are
unit-tested there (`tests/unit/test_{snake,breakout,dodger}.c`). The five
files here (`cads_game.c` plus one file per cartridge) are only canvas
rendering, input polling, and per-cartridge tick timing - the same split
every M5 network watcher already established between its toolbox parser
and its `explorer_*_demo.c`.

## Why is it shaped this way?

**Four real views, not one view with an internal "mode".**
`apps/settings/README.md` already documents the constraint this works
around: the soft-key strip is only reapplied by the compositor when the
*current view* changes - a view has no supported way to relabel its own
strip while it stays current. Snake wants Up/Down/Left/Right, Breakout
wants Left/Right, Dodger wants Up/Down, and the select screen wants
Up/Down/Play; a single view juggling an internal mode would show the
wrong labels for three of those four states. Four pushed views, each with
its own `cads_view_set_softkeys()` call, is the same fix `apps/settings`
already used for its confirm dialog - not a new pattern.

**`cads_game_tick()` checks which view is actually current before
advancing anything.** The original single-game version of this file
ticked its one game unconditionally, which was harmless with nothing else
to tick. With four cartridges, doing that would let Snake keep dying to
a wall it can't be seen hitting while the player is looking at Breakout -
a real gameplay bug, not just wasted cycles - so `cads_game_tick()` reads
`cads_view_dispatcher_current_id()` first and only steps the one on
screen. `cads_desktop_tick()`/`cads_gpio_tick()` don't need this because
neither has a sibling state machine competing for the same tick call.

**Each cartridge gates its own physics/movement step against its own
fixed interval** (`CADS_GAME_SNAKE_STEP_MS` = 150, `_BREAKOUT_STEP_MS` =
20, `_DODGER_STEP_MS` = 30 - see `cads_game_internal.h`), tracked with a
`last_step_ms` field and compared against `now_ms` on every call, rather
than stepping once per `cads_game_tick()` call. `apps/bringup/tasks.c`
calls tick roughly every 10 ms, but "roughly" is not "exactly", and a
toolbox step function that moves a fixed number of grid cells or pixels
per call needs a controlled cadence to feel like a consistent game speed
regardless of how often the main loop happens to actually run.

**Breakout's paddle and Dodger's player are polled, not event-driven.**
Snake's direction only changes on a `Press` edge - one key, one turn - but
a paddle or a dodging player wants to keep moving for as long as the key
stays down, which `cads_input_is_down()` reports directly; approximating
that from `Press`/`Repeat` events would be more code for a worse fit.
Both poll once per physics step, inside the same interval gate above, so
paddle/player speed is independent of the tick-call rate for the same
reason ball/obstacle speed is.

**Registering five views (one select screen, four cartridges) instead of
one bumped the app tree's view-table capacity from 10 to 14** in
`apps/bringup/explorer_app_demo.c` and its mirror in
`tests/unit/test_app_tree.c`. That capacity has a documented history of
silently dropping registrations once real (`cads_view_dispatcher_add()`
returns `false` past capacity rather than growing or asserting, which
once meant the whole menu was unreachable for an entire session before
anyone pressed a button that would have noticed) - both constants were
updated together this time, and `tests/unit/test_app_tree.c` gained a
test that actually presses OK on the select screen and checks the
resulting view id, rather than only checking every id is *registered*.

## How do I use it?

```c
#include "cads_game.h"
#include "cads_hal.h"
#include "cads_view_dispatcher.h"

static cads_view_dispatcher_t dispatcher;
static cads_view_entry_t entries[16]; /* >= 5: the select screen + 4 cartridges */
static uint32_t nav_stack[4];

void app_setup(void) {
    cads_view_dispatcher_init(&dispatcher, entries, 16, nav_stack, 4);
    cads_game_init(&dispatcher); /* registers CADS_VIEW_ID_GAME and all 4 cartridges */
}

void app_main_loop(void) {
    for(;;) {
        uint32_t now = cads_hal_ticks_ms();
        /* Called once per iteration, alongside cads_desktop_tick()/
         * cads_gpio_tick() - advances whichever cartridge is currently
         * on screen, a no-op otherwise. */
        cads_game_tick(now);
        cads_hal_delay_ms(10u);
    }
}
```

`apps/menu` already does exactly this behind `CADS_APP_GAME_ENABLED`; the
row it adds is titled "Reflex Test" and pushes `CADS_VIEW_ID_GAME`, the
select screen - not any cartridge directly.

## What are the limits?

- **Static, single instance, every level.** The select screen and all
  four cartridges are file-static, like every other view in this tree -
  there is no way to run two independent arcades, and `cads_game_init()`
  is not guarded against being called twice.
- **No persistence anywhere.** Reflex Test's `best_ms`, Snake's/
  Breakout's/Dodger's high scores - none of it touches `modules/storage`;
  a reboot, or even just leaving a cartridge and coming back, resets that
  cartridge to a fresh game (`enter()` always calls that cartridge's own
  `reset()`), the same RAM-only situation `apps/settings` documents for
  its own shadow state.
- **Not a security-grade RNG anywhere.** Reflex Test's armed delay,
  Snake's food placement and Dodger's gap heights all come from a local
  32-bit xorshift seeded from `cads_hal_ticks_us()` - unpredictable enough
  for a game, not intended for anything where that has to resist an
  adversary.
- **Breakout and Dodger treat the ball/player as a point or a fixed-size
  box**, not a physically simulated circle - collision is exact grid/AABB
  math, not anything with restitution, spin, or acceleration.
- **Snake's grid and Dodger's obstacle count are fixed at compile time**
  (`CADS_SNAKE_MAX_LENGTH` = 64, `CADS_DODGER_OBSTACLE_COUNT` = 3) - a
  snake that reaches the length cap simply stops growing further rather
  than the game ending, and Dodger never queues more than three obstacles
  in flight at once.
- **Cannot be exercised end to end from a host shell.** Every cartridge
  reads the real physical buttons through the normal input service, not
  through the separate bring-up explorer console that `scripts/board_cmd.py`
  talks to - there is no remote way to inject a synthetic button press
  into the running app tree. Host-side logic tests
  (`tests/unit/test_{snake,breakout,dodger,app_tree}.c`) and hardware
  smoke checks (boot self-test, the `d` command running fault-free) are
  what this project's own agent tooling can verify; a human at the bench
  pressing buttons is the remaining step for full end-to-end confirmation.
