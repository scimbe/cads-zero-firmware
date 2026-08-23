<div align="center">

# CaDS Zero

**A firmware for the ITSboard — 480×320 in colour, with Ethernet.**

`#204C86` · `#B5C4D8` · `#9CB33B` — and a lion.

</div>

---

## What this is

A clean-room firmware for a NUCLEO-F429ZI with an ITS adapter board and a
Waveshare 4" touch shield, built along the lines of what makes a Flipper Zero
pleasant: a small kernel, a GUI framework, a menu of self-contained apps, and a
mascot with an opinion about how you are treating it.

**Clean room.** No code is taken from `flipperzero-firmware`. The ideas are fair
game; the source is not. Everything here is written against this board's
hardware, with the prefix `cads_` and a lion named Leo where the original has a
dolphin.

**What ports and what does not.** The OS, GUI, app model, storage and CLI port
fine. Sub-GHz, NFC, 125 kHz RFID, infrared and iButton do not — those are
transceivers this board does not have, and no software substitutes for missing
silicon. In their place this board has something a Flipper does not: **100 Mbit
Ethernet**, a **480×320 colour touchscreen**, **2.8× the clock**, and a
**DMA2D graphics accelerator**. The roadmap leans into those.

See [`docs/HARDWARE.md`](docs/HARDWARE.md) for the full comparison and the
measured numbers.

## Status

M0 (bring-up), M2 (kernel), M4 (storage) and M7 (tests/CI) are complete. M3
(input/GUI) and M6 (GPIO/timing apps) are functionally done, each with one
`[!]` item needing a human's hands at the bench (touch navigation, a
walkthrough with a jumper wire). M5 (network) is the largest milestone by far
— DHCP, screen streaming, an HTTP status page, and a passive recon suite
(ARP/L2/DHCP/SSDP watchers, traffic stats, a MAC table, ping/traceroute/iperf,
Wake-on-LAN) — all built and unit-tested; its own hardware gate needs a DHCP
server this bench doesn't have. M1's DMA2D path is deliberately deferred (the
software path already runs the SPI bus at 97% of theoretical). Every one of
these is tracked, with the reasoning behind each `[!]`, in
[`docs/ROADMAP.md`](docs/ROADMAP.md).

The on-target self test, **verified on real hardware**, still passes in full:

```
ok 1 - SysTick advances at 1 kHz
ok 2 - DWT microsecond clock agrees
ok 3 - canvas 4 bpp pixel round trip
ok 4 - canvas fill_rect handles unaligned edges
ok 5 - canvas clipping confines drawing
ok 6 - full screen flush transfers every pixel
ok 7 - dirty rectangle limits the transfer
ok 8 - faster SPI divider roughly doubles throughput
ok 9 - adapter output banks driven without fault
ok 10 - reached the end of the self test
# 10/10 passed
# RESULT: PASS
```

| Resource | Used | Available |
|---|---|---|
| Flash (bank 1) | 222.1 KB | 1 MB |
| SRAM | 143.1 KB | 192 KB |
| CCM | 5 KB | 64 KB |

Measured display throughput: **342 kpixel/s** at the safe `/16` SPI divider,
i.e. 448 ms for a full screen — see
[`docs/reference/measurements.md`](docs/reference/measurements.md) for the
full set of measured numbers, and
[`docs/reference/explorer-console.md`](docs/reference/explorer-console.md)
for what every bring-up console command does.

## Build

```bash
git clone --recurse-submodules <this repo>
cd cads-zero

scripts/build.sh              # firmware for the ITSboard
scripts/flash.sh              # write it over ST-Link
scripts/board_test.py         # build, flash, and run the on-target gate
```

The Arm GNU toolchain is picked up from the vcpkg artifact tree that the Keil
Studio extension manages; set `CADS_ARM_TOOLCHAIN_BIN` to override.

## Design notes

**Why a 4 bpp indexed framebuffer.** A 480×320 RGB565 buffer is 300 KB and the
part has 192 KB of DMA-capable SRAM, so retained truecolour was never an option.
Indexed at 4 bpp costs 75 KB, leaves room for lwIP, and lets DMA2D expand
through a CLUT in hardware on the way out. Sixteen colours is not a hardship for
this kind of UI — the device this takes its cues from is monochrome.

**Why the display driver looks paranoid about the SPI bus.** `SPI1_MOSI` and
`ETH_RMII_CRS_DV` are the same physical pin on this board, and only one
alternate function can own it. See [`docs/HARDWARE.md` §6](docs/HARDWARE.md) —
it is the single most consequential fact about this hardware.

**Why every milestone ends at the bench.** The display bus is write-only, so
software cannot ask the panel whether it worked. `scripts/board_test.py` flashes
the real board and reads a TAP stream back over the ST-Link's serial port; a
build that has never met the hardware does not count as passing.

## Layout

```
core/       HAL interface, kernel primitives
gui/        canvas, views, widgets
services/   storage, network, notification, CLI, Leo
apps/       desktop, menu, settings, tools
targets/
  itsboard/ STM32F429 HAL, startup, linker script
  sim/      SDL2 simulation of the whole rig
lib/        CMSIS, FreeRTOS, lwIP, littlefs, Unity (submodules)
scripts/    build, flash, on-target test, asset pipeline
docs/       ROADMAP, HARDWARE, SAFETY
```

## Safety

[`docs/SAFETY.md`](docs/SAFETY.md) is binding. The short version: SWD pins and
the HSE input are never reconfigured, PF/PG stay inputs, flash writes never go
below `0x08120000`, there is no mass erase, and the panel's power and gamma
registers are left exactly as the module vendor set them.

## Licence

MIT. See [`LICENSE`](LICENSE).

Third-party components are used as submodules under their own licences:
CMSIS (Apache-2.0), STM32 CMSIS device headers (Apache-2.0), FreeRTOS-Kernel
(MIT), lwIP (BSD-3), littlefs (BSD-3), Unity (MIT).

The CaDS mark belongs to the CaDS group (communication and distributed systems)
at HAW Hamburg and is used here for a project of theirs.
