# CaDS Zero

**A Flipper-Zero-class handheld firmware for the ITSboard — 480×320 in colour, with Ethernet.**

A clean-room firmware for a NUCLEO-F429ZI carrying an ITS adapter board and a
Waveshare 4" resistive touch shield. It borrows what makes a Flipper Zero
pleasant to use — a small kernel, a GUI framework, a menu of self-contained
apps, a mascot with opinions — and builds it against hardware that is different
in ways that matter.

The mascot here is **Leo**, a lion, and the branding is CaDS.

---

## Start here

<div class="grid cards" markdown>

- :material-school: **[Tutorials](tutorials/index.md)**

    Never built this before. Get an image onto the board and read the result.

- :material-wrench: **[How-to guides](how-to/index.md)**

    Build, flash, run the hardware gate, attach a debugger, work an issue.

- :material-book-open-variant: **[Reference](reference/index.md)**

    Pin maps, memory layout, APIs, measured numbers, safety rules.

- :material-lightbulb: **[Explanation](explanation/index.md)**

    Why the framebuffer is 4 bpp, why PA7 is the most important pin on the
    board, what can and cannot port from a Flipper.

</div>

---

## The honest summary

**What ports.** The operating system, the GUI framework, the app model,
storage, the CLI, the desktop and its mascot, games and utilities. All of it is
software over a display and some buttons, and this board has a better display
than the original.

**What does not port.** Sub-GHz radio, NFC, 125 kHz RFID, infrared and iButton.
Those are transceivers the STM32F429 does not have and no amount of software
substitutes for missing silicon. Roughly half of what a Flipper Zero is *for*
is therefore off the table, and pretending otherwise would waste everyone's time.

**What this board has instead.** 100 Mbit Ethernet, a 480×320 colour
touchscreen, 2.8× the clock speed, twice the flash, and a DMA2D graphics
accelerator. The roadmap spends its effort there: a CLI over TCP, a web status
page, screen streaming to a host — things a Flipper cannot do at all.

See [What ports, and what cannot](explanation/what-ports.md) for the full
accounting.

---

## Status

Milestone 0 is complete and verified on the physical board.

<div class="measured" markdown>

**Measured on hardware, 2026-08-17** — 9/9 assertions passed.

| | |
|---|---|
| Full-screen flush | 448 233 µs → **342 kpixel/s** |
| 40×40 dirty rectangle | 4 717 µs |
| SysTick over 50 ms | 50 ms, exact |
| DWT over 50 ms | 50 014 µs, 0.03 % error |
| Flash used | 13.4 KB of 1 MB |
| SRAM used | 107.7 KB of 192 KB, 86.8 KB heap free |

</div>

342 kpixel/s is 97 % of what the display bus can theoretically carry, so the
driver is not the bottleneck — the bus is. That single number shaped the whole
graphics design; see [Why dirty rectangles are
mandatory](explanation/dirty-rectangles.md).

Progress and open work: [Roadmap](ROADMAP.md) ·
[Issues](https://github.com/scimbe/cads-zero/issues)

---

## How this project is run

- **The maintainer holds the hardware exclusively.** Only one thing may drive
  the ST-Link at a time. Parallel agents write code and tests; the maintainer
  flashes, runs the gate, and merges.
- **Every milestone ends at the bench.** The display bus is write-only, so
  software cannot ask the panel whether it worked. A milestone is done when
  `scripts/board_test.py` passes against the real board, not when CI is green.
- **Work packages are issues.** [`docs/ROADMAP.md`](ROADMAP.md) is the source of
  truth and `scripts/sync_github.py` mirrors it into the tracker. Issues
  labelled `swarm-ready` are self-contained and hardware-free; `hardware-gate`
  issues are maintainer-only.
- **[Safety rules](SAFETY.md) are binding.** They exist because a handful of
  things on this board are genuinely destructible.
