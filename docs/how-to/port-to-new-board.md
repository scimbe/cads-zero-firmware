# Port to different hardware

What has to change when the ITS adapter, the Nucleo, or the panel is swapped —
and, more usefully, what does not.

## The short version

Everything above `core/cads_hal.h` is portable. A port means writing one new
directory under `targets/` and changing nothing else.

```
targets/<board>/
  board.h            pin map, clock tree, peripheral choices
  hal/*.c            the implementation of core/cads_hal.h
  hal_board_info.c   the capability descriptor
  linker/*.ld        memory map
  startup/*.c        reset entry and vector table
  main.c
```

If a port requires editing `gui/`, `services/` or `apps/`, that is a bug in the
abstraction rather than a property of the new board — raise it.

## Step 1: describe the board as data

`cads_hal_board_info()` returns a `cads_board_info_t`. Layers above ask it
questions instead of assuming answers:

```c
const cads_board_info_t* board = cads_hal_board_info();

if(board->has_network) { /* show the network indicator */ }

int16_t strip_height = board->display_height / 8;      /* not 40 */
uint8_t keys = board->button_count;                    /* not 8  */
```

The fields that are easy to get wrong:

- **`display_readable`** — false on the ITSboard, because the shield drives the
  panel through a shift register chain with no return path. Anything wanting a
  read-modify-write on video memory has to keep its own copy. A board with a
  readable panel can set this true and let widgets take the shortcut.
- **`display_pixels_per_second`** — measure it, do not calculate it. On this
  board it is 342 000, which makes a full screen 448 ms and is the single
  reason the canvas tracks dirty rectangles. A faster panel changes what the
  GUI can afford, and it should be able to find that out.
- **`button_count`** — the soft-key strip sizes itself from this. A board with
  four buttons gets four cells, not eight with half of them blank.

## Step 2: the compile-time maxima

`CADS_DISPLAY_WIDTH` and `CADS_DISPLAY_HEIGHT` exist only so the canvas can
allocate its framebuffer statically — there is no allocator on a device like
this, so the linker has to know the size. Override them per target:

```cmake
target_compile_definitions(cads_hal_myboard PUBLIC
    CADS_DISPLAY_WIDTH=320
    CADS_DISPLAY_HEIGHT=240)
```

Then check the arithmetic still fits. At 4 bpp the buffer is
`width * height / 2` bytes; see [Why 4 bpp](../explanation/why-4bpp.md) for why
that format was chosen and what the alternatives cost.

**Layout code must use `board->display_width`, not the macro.** The macro is
the buffer's size; the descriptor is the panel's size, and on a board whose
panel is smaller than the maximum they differ.

## Step 3: implement the HAL

`core/cads_hal.h` is the entire contract. Roughly forty functions in seven
groups: lifecycle, time, console, display, touch, adapter I/O, indicators.

The two that carry real subtlety:

**`cads_hal_display_blit()`** must accept a buffer that stays valid until
`cads_hal_display_busy()` goes false, and on hardware that buffer has to live
in memory the DMA controller can reach. On the STM32F4 that excludes CCM
entirely — a DMA transfer from `0x10000000` silently produces nothing. The
linker script places the framebuffer and staging buffers in a `.dmaram`
section for exactly this reason.

**`cads_hal_console_read()`** must not lose bytes. It is interrupt driven with
a ring buffer on this board, and that is not gold plating: the STM32F4 USART
has a one-byte receive register with no FIFO, so at 115200 baud a byte lands
every 87 µs and any slower polling loop drops characters. That bug cost an
afternoon here, presenting as a display fault — see
[the hardware notes](../HARDWARE.md).

## Step 4: pin conflicts are a design input, not a detail

Read the target's datasheet alternate-function table before assigning
anything, and check whether two peripherals you need can actually coexist.

On this board they cannot: `SPI1_MOSI` and `ETH_RMII_CRS_DV` are both PA7, and
`ETH_RMII_CRS_DV` has no alternative location on the STM32F429. The display and
the Ethernet MAC therefore cannot both own the pin, which shapes the whole SPI
driver. See [the PA7 conflict](../explanation/pa7-conflict.md).

A different board may have no such conflict, in which case
`cads_hal_spi_claim_bus()` and its partner are empty functions and everything
above is unaffected. That is the abstraction earning its keep.

## Step 5: prove it on the hardware

A port is not done because it compiles. `apps/bringup` runs the same self test
on any board and emits TAP over the console; `scripts/board_test.py` reads it
and fails on any assertion that did not arrive.

Add a target-specific check for anything unusual about the new board, and
photograph the panel — for a write-only display that is the only way to catch a
mirrored scan direction or a swapped colour channel. Both have happened here,
and both passed every software test at the time. See
[Run the hardware gate](board-test.md).

## What the simulator gives you for free

`targets/sim/` implements the same HAL against SDL2. Anything portable can be
developed and tested on a host before the new hardware exists, or while someone
else has it on their bench — which is also what lets several people work on the
firmware when there is only one board.
