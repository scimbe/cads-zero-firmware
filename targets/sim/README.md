# targets/sim — the host simulator

## What is it?

The second implementation of `core/cads_hal.h`. It runs the whole firmware —
bring-up, canvas, fonts, input service, explorer — as an ordinary desktop
process, with the 480x320 panel in an SDL2 window, the touch panel on the
mouse, the ITS adapter's buttons on the number keys and its output banks drawn
as indicator cells beside the panel. The console is stdout and stdin, so
`scripts/board_test.py`-style TAP consumers work against it unchanged.

Nothing above the HAL knows this backend exists: `targets/sim/main.c` is the
mirror image of `targets/itsboard/main.c` and calls the same
`cads_bringup_run()`.

## Why is it shaped this way?

**The event pump lives in the HAL, not in `main()`.** `cads_bringup_run()` never
returns — it self tests and then sits in the explorer's command loop forever —
so there is no host event loop to hand time to SDL. If the pump were in
`main()`, the window would never be serviced and macOS would grey it out and
offer to kill the process. It therefore hangs off the calls the firmware already
makes constantly: `cads_hal_delay_*`, `cads_hal_touch_read`,
`cads_hal_adapter_inputs`, `cads_hal_port_read`, `cads_hal_display_blit`. That
is also the honest place for it: those are precisely the points where firmware
on the real board is waiting for the outside world.

**Pixels are unswapped byte-wise, not halfword-wise.** The palette in
`gui/canvas.c` is held pre-swapped to big-endian RGB565, because the panel wants
the high byte first and the SPI DMA emits bytes in memory order. The blit here
reads `source[0] << 8 | source[1]` rather than swapping a `uint16_t`, so the
conversion is a property of the wire format instead of a property of this host's
endianness.

**The indicators exist because `cads_hal_adapter_outputs()` is write-only.** On
the board those sixteen bits drive LEDs a human can see. Without the cells in
the side panel the only way to know what an application put on OUT0..15 would be
a debugger, and the simulator would be less observable than the hardware it
replaces.

**The screenshot fires on quiescence, not on a timer.** `--screenshot` waits
until the panel has gone untouched for a while and captures that frame. A fixed
delay would capture whatever happened to be half drawn at that instant and give
a different image on a faster host; quiescence is a property of the application,
so the same build yields the same image. That is what makes golden-image tests
possible later.

**There is a small private font in `hal_sim.c`.** Labelling the indicators with
`gui/fonts` would make the target layer depend on a module that depends on the
target layer — the exact cycle `docs/reference/module-layout.md` forbids. The
27 glyphs the labels need are cheaper than the inversion.

## How do I use it?

Configure without the ARM toolchain file and the host target is selected
automatically:

```sh
cmake -S . -B build/sim -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/sim
./build/sim/targets/sim/cads-zero-sim
```

SDL2 is found through `sdl2-config.cmake` if the distribution ships one and
through pkg-config otherwise. On macOS, `brew install sdl2` covers both.

Controls:

| Input | Meaning |
|---|---|
| left mouse button on the panel | touch down / drag / up |
| keys `1`..`8` | adapter inputs S0..S7 (bit set = pressed) |
| `F1`..`F6` | adapter interrupts INT0..5 |
| space | the Nucleo's blue USER button |
| `Esc` or `Q` | quit |

Options:

```
--screenshot <file.bmp>    save the first quiescent panel frame and exit
--screenshot-idle <ms>     how long the panel must be still, default 250
--screenshot-timeout <ms>  give up if nothing is drawn, default 10000
--scale <1..4>             window magnification, default 1
```

Screenshot runs select SDL's dummy video driver unless `SDL_VIDEODRIVER` is set,
so they work over ssh and in CI:

```sh
./build/sim/targets/sim/cads-zero-sim --screenshot /tmp/splash.bmp </dev/null
```

The explorer reads stdin, so the simulator drives from a pipe as well as from a
keyboard:

```sh
printf 'i\no 1234\np 3\n' | ./build/sim/targets/sim/cads-zero-sim
```

## What are the limits?

- **No timing fidelity.** There is no SPI, no DMA and no 74HC4094 chain, so a
  flush that costs ~440 ms on the board costs about a millisecond here.
  `cads_hal_display_set_fast_clock()` is a no-op, which means the bring-up's
  "faster SPI divider roughly doubles throughput" assertion cannot pass in this
  backend — it reports `not ok 8` and the run ends `RESULT: FAIL` while the
  other nine tests pass. That number is a property of the hardware, not a bug
  in the simulator.
- **`cads_hal_display_busy()` is always false.** The blit copies synchronously,
  so a caller that recycles a buffer before the transfer finished gets away with
  it here and fails on the board.
- **One clock.** `cads_hal_ticks_ms()` and `cads_hal_ticks_us()` both come from
  `CLOCK_MONOTONIC`, so the bring-up's cross-check between SysTick and the DWT
  counter is trivially satisfied and proves nothing here.
- **Four synthetic GPIO ports.** `cads_hal_port_*` exposes D, E, F and G only —
  the adapter's banks, with the same active-low sense as the board, so the
  explorer's `i` and `w` commands do something meaningful. Everything else the
  hardware explorer exists to discover is not here to be discovered.
- **`cads_hal_pin_is_reserved()` always returns false.** There is no SWD to
  lose, no HSE input to fight and no RMII pair to disturb.
- **The screenshot is a BMP** whatever the file is called; SDL writes no other
  format and pulling in an image library for one debug feature is not worth it.
- **No `cads_hal_console_dropped()` / `cads_hal_console_overruns()` counters.**
  The host's tty buffers for us, so both return zero and the class of bug they
  were added to catch is invisible here.
