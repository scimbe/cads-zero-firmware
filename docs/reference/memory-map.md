# Memory map

Where everything lives on the STM32F429ZI, and which of it a DMA controller can
reach — the constraint that shapes most of the layout.

## Flash

2 MB in **two banks**, which is why a firmware update cannot destroy the
filesystem: the part supports read-while-write across banks, and nothing in
this repository ever issues a chip erase.

| Region | Address | Size | Sectors | Use |
|---|---|---|---|---|
| `FLASH_APP` | `0x08000000` | 1024 KB | bank 1, 0–11 | firmware |
| reserved | `0x08100000` | 128 KB | bank 2, 12–16 | left erased |
| `FLASH_FS` | `0x08120000` | 896 KB | bank 2, 17–23 | littlefs volume |

Sector geometry within a bank is not uniform: sectors 0–3 are 16 KB, sector 4
is 64 KB, sectors 5–11 are 128 KB. The filesystem uses only the 128 KB sectors
so its block size is constant.

The linker asserts the firmware fits in bank 1, `scripts/flash.sh` refuses an
image larger than 1 MB, and CI fails if any section lands above `0x08100000`.
Three independent checks, because overwriting the filesystem is silent.

## RAM

| Region | Address | Size | DMA | Use |
|---|---|---|---|---|
| SRAM1+2+3 | `0x20000000` | 192 KB | **yes** | framebuffer, staging, buffers |
| CCM | `0x10000000` | 64 KB | **no** | task stacks, main stack |

**CCM is invisible to every DMA controller on this part.** A transfer sourced
from `0x10000000` silently produces nothing — no fault, no error flag, just
wrong output. That single fact drives the split:

- anything a peripheral will read goes in `.dmaram`, explicitly, so its
  placement is visible in the map file;
- anything the CPU alone touches goes in CCM, where it costs nothing scarce.

## Current usage

Measured after the kernel landed:

| | Used | Available |
|---|---|---|
| Flash (bank 1) | 84 KB | 1 MB |
| SRAM | 112 KB | 192 KB |
| CCM | 5 KB | 64 KB |

SRAM breaks down as 75 KB framebuffer (480×320 at 4 bpp) plus 30 KB of RGB565
staging buffers plus statics. CCM holds three task stacks and nothing else —
there is **no kernel heap**, because FreeRTOS is configured for static
allocation only. See [the kernel configuration](https://github.com/scimbe/cads-zero/blob/main/modules/kernel/src/FreeRTOSConfig.h).

## Sections

| Section | Region | Notes |
|---|---|---|
| `.isr_vector` | flash | 107 entries, generated from the CMSIS header |
| `.text` `.rodata` | flash | |
| `.data` | SRAM (loaded from flash) | |
| `.ramfunc` | SRAM (loaded from flash) | flash erase/program, so it never executes from the memory it is writing |
| `.bss` | SRAM | |
| `.dmaram` | SRAM | framebuffer and staging, placement guaranteed |
| `.ccm` | CCM | task stacks |
| main stack | CCM, top 4 KB | grows down, away from `.ccm` |

The linker asserts `.ccm` does not collide with the main stack and that at
least 48 KB of SRAM remains free, so a change that quietly squeezes out lwIP
fails the build rather than the field.
