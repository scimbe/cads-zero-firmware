# Clean room, and what that means here

## The decision

This firmware contains **no code from `flipperzero-firmware`**. Not copied, not
adapted, not transliterated. The concepts are studied; the source is not used.

That was a deliberate choice with a licensing consequence attached.

## Why it matters

`flipperzero-firmware` is **GPL-3.0**. Vendoring any meaningful part of it —
`furi/`, the GUI stack, the app framework — makes this project GPL-3.0 too, and
that obligation travels with every copy that is distributed, private repository
or not.

That may be perfectly acceptable. It simply has to be a decision rather than an
accident, because it is not reversible after the fact: you cannot un-GPL a
codebase that has GPL code in its history.

The choice here was clean-room, so this project is **MIT** and the licence is a
free variable for whatever it grows into.

## What "clean room" means in practice

**Allowed.** Reading the upstream project to understand what a good handheld
firmware does. Adopting architectural ideas: a service registry, a view
dispatcher, a mascot with a mood. Naming things after the concept they
implement. Reading the public documentation.

**Not allowed.** Copying a function, a struct layout, a constant table, or a
file and renaming it. "Rewriting" something with the original open beside you,
which is transliteration and not independent work.

**The tell.** If the answer to "why is it shaped like this?" is *because that is
how they did it*, it is a copy. If the answer is *because this board's display
is write-only and 448 ms per frame*, it is independent work.

## How that shows up in the code

The prefix is `cads_`, not `furi_`. Not decoration — it is the visible marker
that these are different implementations with different constraints.

The mascot is **Leo, a lion**, taken from the CaDS group's own mark, rather than
a dolphin.

More substantively, the architecture diverges wherever the hardware does:

| Upstream shape | Here | Because |
|---|---|---|
| 128×64 1 bpp canvas | 480×320 4 bpp indexed | 15× the pixels, and RGB565 would not fit in RAM |
| u8g2 as the drawing library | own canvas | u8g2 is monochrome-oriented; the constraint here is nibble packing and DMA staging |
| Full-buffer repaint | dirty-rectangle bounding box | a full screen costs 448 ms on this bus |
| Buttons | touch, plus 14 adapter lines | there are no buttons |
| SD card storage | littlefs in flash bank 2 | there is no card |
| Sub-GHz / NFC / IR services | network services | there are no radios |

Nearly every one of those rows is a place where copying upstream would have
produced something that does not work here.

## What is vendored, and under what

Third-party components are git submodules under permissive licences, all
compatible with MIT:

| Component | Licence | Why |
|---|---|---|
| CMSIS_6 | Apache-2.0 | Cortex-M core headers |
| cmsis_device_f4 | Apache-2.0 | STM32F4 register definitions |
| FreeRTOS-Kernel | MIT | scheduler |
| lwIP | BSD-3 | TCP/IP |
| littlefs | BSD-3 | power-loss-resilient filesystem for raw flash |
| Unity | MIT | unit test framework |

Notably **not** vendored: ST's HAL. The drivers here are written against the
registers. That is not purity — it is that the ST HAL's init structs hide which
bits actually changed, and on a board where one pin is contested between two
peripherals, knowing exactly which register writes happen is the difference
between a working display and a silent one.

## The one piece of borrowed data

`hal_display.c` contains the ILI9486 power, gamma and timing register table
from the Waveshare module's own example code.

That is **hardware configuration data, not creative code** — the `VGH`/`VGL`
and `VCOM` values are electrical parameters of this specific panel. Re-deriving
them from the datasheet would risk getting a drive voltage wrong, and wrong
drive voltages are one of the few ways software can physically damage a TFT.
Reproducing a vendor's known-good register sequence for their own module is the
correct engineering call, and it is documented as such where it appears.
