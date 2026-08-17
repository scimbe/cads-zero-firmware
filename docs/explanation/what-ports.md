# What ports from a Flipper, and what cannot

The honest accounting, made once so it does not have to be re-argued.

## The two devices

| | Flipper Zero (STM32WB55RG) | ITSboard (STM32F429ZI) |
|---|---|---|
| Core | Cortex-M4 @ 64 MHz | Cortex-M4F @ **180 MHz** |
| Flash | 1 MB | **2 MB**, dual bank |
| RAM | 256 KB | 192 KB + 64 KB CCM |
| Display | ST7565R 128×64 mono | **ILI9486 480×320 colour** |
| Input | 5-way pad + back button | **resistive touch** + 14 adapter lines |
| Graphics accel | none | **DMA2D (Chrom-ART)** |
| Network | BLE (second core) | **100 Mbit Ethernet** |
| Storage | microSD | internal flash (littlefs) |
| Sub-GHz | CC1101 | — |
| NFC | ST25R3916 | — |
| 125 kHz RFID | yes | — |
| Infrared | TSOP + LED | — |
| iButton / 1-Wire | yes | — |
| USB | device, HID/CDC | OTG FS present, unused |

## What ports cleanly

Everything that is software over a display and some inputs:

- **The kernel.** Threads, mutexes, queues, timers, event flags, a publish
  /subscribe bus, a service registry. Generic RTOS material on a generic
  Cortex-M4.
- **The GUI framework.** Canvas, views, a view dispatcher, a compositor with
  layers, menus, dialogs, text boxes, a status bar.
- **The app model.** Self-contained applications with their own state, launched
  from a menu, returning to the desktop.
- **Storage.** A filesystem, a path API, persisted configuration.
- **The CLI.** A command table over a serial console.
- **The desktop and the mascot.** Leo instead of a dolphin — a state machine
  with a mood, a level, and animations.
- **Games and utilities.** Anything that only needs a screen and a button.

This board's display is better in every dimension: 15× the pixels, colour, and
a touch panel. Nothing about the UI layer is harder here.

## What cannot port

**Sub-GHz, NFC, 125 kHz RFID, infrared, iButton.** These are not software
features. Each is a dedicated transceiver — a CC1101, an ST25R3916, an analogue
125 kHz front end, an IR LED and demodulator, a 1-Wire driver — and the
STM32F429 has none of them. There is no firmware trick that substitutes for an
absent radio.

That is a real loss, and it is roughly half of what a Flipper Zero is *for*. Any
plan that quietly implies otherwise is wrong, and it is better to say so at the
start than to discover it at milestone five.

**BLE.** The WB55's second core is a radio coprocessor. The F429 has no radio.

**USB HID.** The F429 *does* have USB OTG FS on PA11/PA12 (Nucleo CN13), so
"BadUSB"-style features are technically reachable. They are out of scope because
the user has not asked for them and the connector's accessibility under the
stacked shield is unverified.

## What replaces them

The board has one capability a Flipper has no answer to at all: **a wired
network interface**. The roadmap spends its effort there rather than mourning
the radios.

- **CLI over TCP.** The same command table as the serial console, reachable
  over the network.
- **A web status page.** Device state in a browser.
- **Screen streaming.** Push the framebuffer to a host viewer — which also makes
  documentation screenshots and golden-image tests possible for a display whose
  bus cannot be read back.
- **Network tooling as apps.** Ping, port scan, DHCP inspection, packet
  statistics — a genuinely useful handheld for a networks lab, which is what
  this board exists for.

Plus what the extra silicon buys: 2.8× the clock, twice the flash, DMA2D, and
fourteen adapter I/O lines that make a GPIO app actually interesting.

## The honest framing

This is not a Flipper Zero port. It is **a Flipper-Zero-class handheld built for
a networks lab**: the same interaction model and the same feel, with the radios
replaced by a network stack. Framed that way the missing hardware stops being a
gap and starts being a different product.
