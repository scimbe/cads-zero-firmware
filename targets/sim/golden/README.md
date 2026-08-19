# targets/sim/golden — reference images for the golden-image tests

## What is it?

Reference PNGs the simulator's rendering is checked against, plus this file.
Each PNG is the exact frame `cads-zero-sim --screenshot` produces once the
firmware has gone quiescent in a particular, deliberately-chosen state.
`targets/sim/tests/golden_check.py` runs the simulator, decodes the BMP it
writes, and compares it pixel for pixel against the matching PNG here; the
test wiring itself lives in `targets/sim/tests/`, following
`docs/reference/module-layout.md`'s split between a module's data and its
tests, adapted to golden images not being Unity subjects.

Two scenes today:

| File | What it shows | How it is reached |
|---|---|---|
| `splash.png` | The boot splash: the CaDS mark, the lion, the "Z E R O" wordmark. | Default `--screenshot`, no console input. The splash is drawn and flushed, then held for a fixed 1.5 s (`apps/bringup/bringup.c`) before the self test starts drawing over it - comfortably longer than the screenshot's default 250 ms idle window, so quiescence fires during that hold. |
| `bringup_pattern.png` | The bring-up self test's own on-screen test pattern: 16 palette swatches, the four corner markers, the diagonal cross, and the fast-SPI-clock qualification band (`cads_draw_test_pattern()` / `cads_check_fast_clock()` in `apps/bringup/bringup.c`) - this is the closest thing in this codebase to what M7's roadmap entry calls a "colour-bar test pattern". | `--screenshot-idle 2000`, no console input. See "Why is it shaped this way?" below for why 2000 and not the default. |

## Why is it shaped this way?

**There is no `apps/gpio` panel golden here, even though the task this
directory was built for asked for one.** `apps/gpio`, the GUI compositor it
needs (`gui/view`, `gui/widgets`) and the menu that hosts it (`apps/menu`)
are real, present in this repository - but on this branch
(`tester/mdio-diagnostics`) nothing wires them into an executable.
`CMakeLists.txt`'s `cads_apps` library only builds `apps/bringup/*.c`, and
`cads_bringup_run()` never reaches a view dispatcher; it runs the self test
and then hands off straight to `apps/bringup/explorer.c`'s console loop.
The commit that wires the real GUI stack onto both targets
(`Wire the real GUI stack onto hardware: gui/view + gui/widgets + apps/gpio`,
`df3ceca` on `origin/main`) landed on `main` after this branch had already
diverged, so it is not reachable from here without merging a substantial,
unrelated body of work into what is meant to be a self-contained change.
Once that lands on this branch (by merge or by rebase), add
`gpio_panel.png` the same way as the two scenes above: work out what
`--screenshot-idle` (and, if the GPIO view needs a touch/key event to open
from the menu, what to feed on stdin) lands the panel on screen, capture it
with `update_golden`, and add one `cads_add_golden_test(...)` line.

**`bringup_pattern.png` is not reached through explorer.c's `p` command.**
The roadmap entry this directory implements guesses it might be
(`docs/ROADMAP.md`, M7); `apps/bringup/explorer.c`'s `p <n>` only reaches
`cads_pattern()`'s six scenes (black / blue / green / quadrants / stripes /
splash / type specimen) and none of them is the 16-swatch pattern with the
corner markers and diagonals - that pattern is drawn directly by
`cads_draw_test_pattern()`, called from inside the self test
(`cads_check_display_throughput()` and `cads_check_fast_clock()`), with no
`p`-command equivalent at all. What actually puts it on screen is simpler
than a console command: it is the last thing the self test draws before
`cads_tasks_start()` hands off to the explorer, so it is what stays on the
panel indefinitely once the explorer's stdin runs dry. The only lever needed
is `--screenshot-idle`: the default (250 ms) fires during the boot splash's
1.5 s hold, exactly as `splash.png` wants; a value past 1.5 s (2000 ms, for
margin) skips over that hold and lands on the self test's final frame
instead. Verified empirically, twice each, byte-identical BMP output both
times for both scenes - see the golden-image-tests commit message for the
numbers. That also means this scene is a second thing worth watching: if the
self test's drawing order or timing ever changes, this golden fails too, on
top of the "did the UI change" question it exists to answer.

**Exact comparison, no threshold.** `targets/sim/README.md` documents the
screenshot as deterministic - quiescence is a property of the application,
not of a timer - and that held up under repeated, empirical testing (above).
A threshold would exist to absorb noise that this mechanism does not have;
adding one anyway would just make a real regression easier to miss.

**PNG, indexed colour where possible, via Python's `zlib` rather than a
hand-rolled or vendored compressor.** Every pixel any of these scenes can
produce comes from the canvas's fixed palette
(`docs/ROADMAP.md` M1: 4 bpp indexed, at most 16 colours at a time), so PNG's
indexed-colour mode (`golden_check.py`'s `encode_png()`) needs only 1 byte per
pixel before compression, with a true fallback to 8-bit truecolour if a
future scene ever has more than 256 distinct colours. The project has no
vendored PNG library and pulling one in for a debug/test feature is the exact
tradeoff `targets/sim/README.md` already rejected in favour of staying with
BMP inside the simulator itself. Hand-writing a full DEFLATE compressor was
the other option on the table, but PNG's only *simple* fully-manual path -
uncompressed ("stored") deflate blocks - would produce files close to the
size of the BMP they replace (24 bpp truecolour, no compression), the
opposite of the point. The build already has a hard, enforced dependency on
Python 3 (`find_package(Python3 ... REQUIRED)`, top-level `CMakeLists.txt`)
for exactly this kind of host-side tooling, and Python's standard library
ships the real `zlib`/DEFLATE codec - not a new dependency, just using one
this project already requires. `splash.png` is 3.9 KB; the BMP it was
captured from is 460 KB.

## How do I use it?

Run the golden tests as part of the normal host suite:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure -R golden_
```

A failing comparison prints how many of the 153,600 pixels differ and where
it wrote a diff PNG (under the build directory, so it never lands in `git
status`): a dimmed copy of what the simulator actually drew, with every
differing pixel painted solid red, so the mismatch's shape is visible at a
glance without a threshold to tune.

After an intentional UI change, regenerate every golden from the build that
made the change:

```sh
cmake --build build/host --target update_golden
```

or one scene at a time, the same way the test itself invokes the tool:

```sh
python3 targets/sim/tests/golden_check.py update \
    --sim build/host/targets/sim/cads-zero-sim \
    --golden targets/sim/golden/splash.png --name splash --idle 250
```

Then look at the diff before committing it - `update_golden` cannot tell an
intended change from a regression, only a human reviewing what moved can.

## What are the limits?

- **Host-only, like the rest of `targets/sim/`.** These tests need SDL2 and a
  simulator binary; they never run against real hardware, and nothing here
  claims to stand in for the hardware gate `docs/ROADMAP.md` requires.
- **No `apps/gpio` scene yet** - see "Why is it shaped this way?" above.
- **A screenshot only ever captures the 480x320 panel**, not the simulator
  window's side panel (the OUT/IN/INT indicator cells, drawn by
  `targets/sim/hal_sim.c` itself, outside the framebuffer the screenshot
  wraps) - so a golden test here can never catch a regression in those
  indicators. That is a `hal_sim.c` property, not something this directory
  can change without also changing what `--screenshot` captures for every
  other use of it (interactive debugging included).
- **`golden_check.py`'s PNG decoder is not a general one.** It only reads what
  its own encoder writes (8-bit, filter type None, single IDAT stream,
  indexed or truecolour) and raises rather than guessing on anything else -
  correct for round-tripping this directory's own files, wrong tool for
  opening an arbitrary PNG.
- **Adding a scene means finding a new timing/input recipe, not just a new
  file.** There is no general "wait for view X" hook; every scene so far is
  reached by choosing `--screenshot-idle` against the self test's own fixed
  delays. That is workable for two scenes and will need revisiting past a
  handful more.
