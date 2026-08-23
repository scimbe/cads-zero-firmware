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

CI's `default` matrix leg (all optional apps on), 2026-08-23 (after Leo's
Arcade grew from one game view to a select screen plus three new
cartridges — Snake, Breakout, Dodger), read live from that push's own CI
log (run 32646459994):

| | Used | Available | |
|---|---|---|---|
| Flash bank 1 | 226.8 KB (232 284 B) | 1 MB | 22.2 % |
| SRAM | 143.6 KB (147 040 B) | 192 KB | 74.8 % |
| CCM | 5 KB | 64 KB | 7.8 % |

`__cads_heap_size` margin over the linker's `ASSERT(>= 48K)` floor: 416 B,
down from 928 B before the three new games — the number
`scripts/check_ram_budget.py` gates CI on (minimum 256 B). The
`minimal` matrix leg (every optional app off) reports 6 784 B of margin over
the same floor. SRAM is 75 KB framebuffer + 30 KB staging + statics + lwIP's
pool; CCM is three task stacks; there is no kernel heap at all.

Flash usage is *reported* every CI run (`arm-none-eabi-size` to the job
summary) but not *gated* — there is no stored baseline or regression
threshold on it the way there is for the RAM margin above, so unlike SRAM
this number can only be read, not relied on to fail a build on its own.

## What is not measured yet

Stated so nobody mistakes silence for a result:

- Ethernet throughput, and how badly the PA7 time-slicing hurts it. An
  `iperf`-compatible server exists (explorer console command `I`) but this
  bench's agent shell has no network path to the board's segment, so the
  capability has never actually been exercised against a real client.
- Touch latency and repeat accuracy over a long session.
- Flash write endurance and littlefs behaviour across power cuts.
- Power draw.
- ~~Display throughput has not been re-measured since M0/M1~~ — re-measured
  2026-08-23 under real scheduler + live-netif contention (explorer command
  `V`, `apps/bringup/explorer_throughput_demo.c`): **342/342/342 kpixel/s
  (min/avg/max, 18 flushes over 8s)**, identical to the pre-scheduler M0/M1
  number. Expected in hindsight - `cads_canvas_flush()` is one blocking SPI
  transfer per call, so nothing scheduled between calls can show up inside
  it - but this was an assumption until it was actually measured. Ambient
  traffic on this bench is still near-zero, so this exercises real
  scheduler preemption and a live, link-up, polled netif, not an RX ISR
  firing mid-flush specifically; see `docs/ROADMAP.md`'s M1 log entry for
  the full result.

`scripts/board_soak.py` runs the board for hours and watches stack high-water
marks, task count and responsiveness, which covers the failure modes a one-shot
gate cannot.
