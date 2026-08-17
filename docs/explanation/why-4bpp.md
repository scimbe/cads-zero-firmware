# Why the framebuffer is 4 bits per pixel

## The arithmetic that decided it

A 480×320 framebuffer in RGB565 costs:

```
480 × 320 × 2 bytes = 307 200 bytes = 300 KB
```

The STM32F429ZI has 256 KB of RAM, and it is not one pool:

| Region | Size | Address | DMA |
|---|---|---|---|
| SRAM1 + SRAM2 + SRAM3 | 192 KB contiguous | `0x20000000` | yes |
| CCM | 64 KB | `0x10000000` | **no** |

CCM is invisible to every DMA controller on the part, so it cannot hold anything
a peripheral will read. The usable framebuffer budget is therefore 192 KB, and a
truecolour retained buffer does not fit with 108 KB to spare — it does not fit
at all.

## The three options

**Band rendering.** Keep no framebuffer; render a horizontal strip at a time
into a small buffer and push it. Memory-cheap, and it is what most
resource-constrained colour UIs do. The cost is that nothing is retained: every
widget must be able to redraw any sub-rectangle of itself on demand, the whole
GUI becomes immediate-mode, and partial updates get complicated because you must
re-render every widget that intersects the damaged strip. It also interacts
badly with a slow bus — see below.

**8 bpp indexed.** 480 × 320 = 153 600 bytes = 150 KB. It fits in 192 KB, but it
leaves 42 KB for lwIP, the Ethernet DMA descriptors and receive buffers, the
FreeRTOS heap, and everything else. Ethernet is the main reason this board is
interesting; strangling it to get 256 colours is a bad trade.

**4 bpp indexed.** 480 × 320 ÷ 2 = 76 800 bytes = 75 KB. Leaves ~117 KB.

## Why 4 bpp wins

**It fits with room to spare.** Measured after M0: 107.7 KB of SRAM in use
(framebuffer plus staging buffers), **86.8 KB of heap free**. lwIP with a
reasonable pool, ETH descriptors and buffers, and the GUI all fit in that.

**Sixteen colours is not a hardship for this UI.** The device this takes its
cues from is *monochrome*. A palette of sixteen — brand blue, lion blue, accent
green, four greys, a few semantic colours — is luxurious by comparison. And
because it is a palette, a theme change is one table rather than a repaint.

**The hardware likes it.** The F429 has **DMA2D**, ST's Chrom-ART accelerator.
It reads L4 (4 bpp indexed) through a CLUT and writes RGB565, in hardware, while
the CPU does something else. The conversion that an indexed format normally
costs you is close to free on this specific part. Nothing comparable exists on a
Flipper's STM32WB55.

**Retained beats immediate here.** Because the display bus is write-only
(see [Hardware](../HARDWARE.md)), the RAM buffer is the only record of what is on
screen. Having a real framebuffer means damage tracking is a bounding box and a
memcpy-shaped flush, rather than a re-render of every intersecting widget. With
a bus this slow, being able to push *only* what changed is worth more than
colour depth.

## What it costs

Nibble packing. Two pixels share a byte, so a horizontal span has a ragged left
and right edge that need read-modify-write while the middle is a straight
`memset`. `cads_canvas_fill_rect()` handles all three cases, and the M0
self-test asserts on exactly that:

```
ok 4 - canvas fill_rect handles unaligned edges
```

That assertion exists because getting the high and low nibble the wrong way
round produces a subtly mirrored image and no other symptom — the kind of bug
that survives a casual look at the screen.

## The palette

Slots 2, 3 and 4 are the CaDS mark's own colours, so the device and the
documentation look like the same product:

| Slot | Name | Colour | |
|---|---|---|---|
| 2 | `CadsColorBrand` | `#204C86` | CaDS blue, the wordmark |
| 3 | `CadsColorBrandLight` | `#B5C4D8` | lion blue |
| 4 | `CadsColorAccent` | `#9CB33B` | CaDS green, the tagline |

The palette is stored **pre-byte-swapped** to big-endian RGB565, because the
panel wants the high byte first and the SPI DMA emits bytes in memory order.
Storing the swap in the palette turns the per-pixel conversion into a table
lookup and a store, with no byte shuffling on the hot loop.
