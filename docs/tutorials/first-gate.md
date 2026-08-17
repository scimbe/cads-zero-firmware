# Read the on-target test

What the hardware gate checks and how to read its output.

## The output

```
========================================
 CaDS Zero - milestone 0 bring-up
 build Aug 17 2026 17:28:57
========================================
1..9
ok 1 - SysTick advances at 1 kHz
ok 2 - DWT microsecond clock agrees
# systick_ms_over_50ms: 50
# dwt_us_over_50ms: 50014
ok 3 - canvas 4 bpp pixel round trip
ok 4 - canvas fill_rect handles unaligned edges
ok 5 - canvas clipping confines drawing
ok 6 - full screen flush transfers every pixel
# flush_pixels: 153600
# flush_us: 448233
# flush_kpixel_per_s: 342
ok 7 - dirty rectangle limits the transfer
# partial_pixels: 1600
# partial_us: 4717
ok 8 - adapter output banks driven without fault
ok 9 - reached the end of the self test
# 9/9 passed
# RESULT: PASS
```

This is TAP. `1..9` is the plan, `ok`/`not ok` are assertions, `#` lines are
diagnostics. The format is deliberate: "the screen looked right" cannot be a
gate, `ok 4` can.

## What each assertion is defending against

**1 and 2 — the time base.** SysTick and the DWT cycle counter are independent
sources. Checking them against each other catches more than checking either
alone: a wrong PLL multiplier skews both together, a wrong SysTick reload skews
only one. `50014 µs` over 50 ms is 0.03 % error.

**3 — nibble packing.** Two pixels share a byte. Getting the high and low nibble
the wrong way round produces a subtly mirrored image and no other symptom, which
is the kind of bug that survives a casual look at the screen.

**4 — ragged edges.** `fill_rect` uses `memset` for the aligned middle and
read-modify-write for the odd pixel at each end. The test fills a 7-wide
rectangle starting at x=3 so both edges are ragged, and checks the pixels just
outside are untouched.

**5 — clipping.** Draws a full-screen rectangle inside a 50×50 clip and verifies
nothing escaped.

**6 and 7 — the display path, and the numbers that matter.** See below.

**8 — adapter I/O.** Drives the output banks with alternating patterns and reads
the inputs back. Only PD and PE are driven; PF and PG are read-only by policy
([Safety](../SAFETY.md#3-respect-pin-directions-on-the-its-adapter)).

## The numbers

<div class="measured" markdown>

`flush_kpixel_per_s: 342` — the theoretical maximum for this bus at the `/16`
divider is 351 kpixel/s. The driver is at **97 %** of it, so there is no
inefficiency left in software.

`flush_us: 448233` — a full screen costs **448 ms**.

`partial_us: 4717` for 1 600 pixels — the same rate, confirming dirty rectangles
scale linearly rather than silently promoting to full-screen.

</div>

If `flush_kpixel_per_s` drops meaningfully below 340, something has been added
to the per-blit path. If `partial_pixels` ever reads 153600, damage tracking has
broken. See [Why dirty rectangles are
mandatory](../explanation/dirty-rectangles.md).

## When it fails

| Symptom | Likely cause |
|---|---|
| No output at all | Wrong serial port, or a fault before the console came up |
| Plan says 9, fewer arrive | The firmware died part way — attach GDB |
| `not ok 1` or `2` | Clock tree — check `RCC_CFGR` bits 3:2 read `10` |
| `not ok 3` or `4` | Canvas nibble packing |
| `not ok 6` | Damage tracking or the blit path |

[Debug with GDB](../how-to/debug.md) has the register reads for each of these.
