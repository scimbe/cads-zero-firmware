# Measurements

Numbers taken from the physical board, with the conditions they were taken
under. Everything here is measured; nothing is calculated and presented as if
it were measured.

## Display throughput

Conditions: STM32F429ZI at 180 MHz, SPI1 on APB2 (90 MHz), Waveshare 4"
shield, `apps/bringup` self test, 2026-08-17.

| Divider | SPI clock | Full screen | Throughput | Theoretical |
|---|---|---|---|---|
| /16 | 5.625 MHz | **448 233 µs** | **342 kpixel/s** | 351 kpixel/s |
| /8 | 11.25 MHz | **229 526 µs** | **669 kpixel/s** | 703 kpixel/s |

The driver runs at **97 %** and **95 %** of what the bus can carry. There is no
software inefficiency left to find — the shield's shift register chain costs 16
SPI clocks per pixel, and that is the whole story.

Partial transfers scale linearly: a 40×40 rectangle costs 4 717 µs, which is
339 kpixel/s — the same rate. Dirty-rectangle tracking therefore buys exactly
what it looks like it should.

**Consequence.** A full-screen redraw is half a second. Every design decision
in [the canvas](canvas.md) follows from that; see
[why dirty rectangles are mandatory](../explanation/dirty-rectangles.md).

## Time base

| | |
|---|---|
| SysTick over 50 ms | 50 ms |
| DWT over 50 ms | 50 014 µs (0.03 % error) |

Measured against each other deliberately: the two are independent, so a wrong
PLL multiplier skews both together while a wrong reload skews only one. SysTick
has since been handed to the scheduler and the time base derives entirely from
DWT — which also keeps time correctly inside critical sections and ISRs, where
an interrupt-driven counter quietly stops.

## Memory

After the kernel landed:

| | Used | Available | |
|---|---|---|---|
| Flash bank 1 | 84 KB | 1 MB | 8 % |
| SRAM | 112 KB | 192 KB | 57 % |
| CCM | 5 KB | 64 KB | 8 % |

SRAM is 75 KB framebuffer + 30 KB staging + statics. CCM is three task stacks;
there is no kernel heap at all.

## What is not measured yet

Stated so nobody mistakes silence for a result:

- Ethernet throughput, and how badly the PA7 time-slicing hurts it.
- Touch latency and repeat accuracy over a long session.
- Flash write endurance and littlefs behaviour across power cuts.
- Power draw.

`scripts/board_soak.py` runs the board for hours and watches stack high-water
marks, task count and responsiveness, which covers the failure modes a one-shot
gate cannot.
