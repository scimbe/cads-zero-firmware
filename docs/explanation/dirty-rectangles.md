# Why dirty rectangles are mandatory

Not an optimisation. A precondition for the device being usable at all.

## The measurement

<div class="measured" markdown>

Measured on the physical board, 2026-08-17:

| | |
|---|---|
| Full screen, 153 600 pixels | **448 233 µs** |
| Throughput | **342 kpixel/s** |
| 40×40 rectangle, 1 600 pixels | **4 717 µs** |
| Throughput, partial | 339 kpixel/s |

</div>

**A full-screen redraw takes 448 milliseconds.** Just under half a second. At
that rate an interface that repaints every frame runs at 2.2 fps, and every
touch response arrives half a second late.

## Where the time goes

Not in the driver. The bus is the limit, and the driver is already at 97 % of it.

The shield does not wire SPI to the panel directly. MOSI feeds a 74HC4040
counter and two cascaded 74HC4094 shift registers, which assemble a 16-bit
parallel word for the ILI9486:

```
MCU SPI1 ──MOSI──> 74HC4094 ×2 ──D[15:0]──> ILI9486
             │        ▲
             └─SCLK──>74HC4040 ──CLK/16──> latch + panel WR
```

So **one pixel costs 16 SPI clocks**. At the proven-safe divider of `/16`:

```
SPI clock   = PCLK2 / 16 = 90 MHz / 16 = 5.625 MHz
pixel rate  = 5.625 MHz / 16            = 351 kpixel/s   (theoretical)
measured                                = 342 kpixel/s   (97.4 %)
full screen = 153 600 / 342 000         = 449 ms
```

The measurement matches the model to within 3 %, which is the overhead of the
window-setting commands and the per-blit Ethernet arbitration. There is no
driver inefficiency left to find.

## What follows

**Redraw only what changed.** The canvas keeps one bounding box of everything
that has been drawn since the last flush. Every drawing primitive extends it;
`cads_canvas_flush()` converts and pushes exactly that rectangle and then clears
it. A 40×40 update costs 4.7 ms instead of 448 ms — a factor of 95.

The M0 self-test asserts on this directly, because a bug that silently promotes
damage to full-screen would show up only as "the UI feels sluggish":

```
ok 7 - dirty rectangle limits the transfer
# partial_pixels: 1600
```

**Design the UI so damage stays small.** A status bar that repaints its clock
every second should damage the clock, not the bar. A menu that moves a selection
should damage the two rows involved, not the list.

**A single bounding box is a deliberate simplification.** Two small updates in
opposite corners produce a box covering the whole screen. A damage *list* would
handle that better. It is not implemented because the widget layouts in the
roadmap do not generate that pattern, and a list costs complexity in the flush
path. If a future app does generate it, the box becomes a list — but measure
first.

## The ways to go faster

Ranked by effort against payoff.

**Raise the SPI divider.** `/8` doubles throughput to ~684 kpixel/s and halves a
full screen to 224 ms. The limit is the 74HC4094 chain, not the panel, and it is
a staged experiment on real hardware rather than an edit — see
[Safety](../SAFETY.md#5-display-and-touch). `/4` may also work; do not go below it.

**DMA2D for the conversion.** The Chrom-ART accelerator expands L4 through a
CLUT into RGB565 in hardware. This does not make the bus faster, but it frees
the CPU during a flush, which matters once there is a scheduler and a network
stack competing for cycles.

**Overlap conversion with transmission.** Already done: two staging banks, so
band *n+1* is converted while band *n* is going out over DMA.

**Swap SB121/SB122.** Not a display speedup as such, but it removes the
per-blit Ethernet stop/start and lets the display run at a faster divider
without costing the network anything. See [the PA7 conflict](pa7-conflict.md).
