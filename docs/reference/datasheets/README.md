# Primary-source datasheets and schematics

Archived here so they survive independent of any one person's uploads folder,
and so a claim in the docs can always be traced back to the page it came from.
Provided by the user (scimbe), 2026-08-19.

## This board

- **`ITSBRD-schematic-Jaehnichen-HAW-rev02.pdf`** — the authoritative schematic
  for the actual hardware this project targets: the ITS adapter board on a
  NUCLEO-F429ZI, by Tobias Jähnichen, HAW Hamburg Labor für Technische
  Informatik, revision 02, 2020-12-16. Five sheets: top view, Nucleo-144
  connection, GPIOs (page 3 — the ground truth for `targets/itsboard/board.h`),
  busses (I2C/RS232/SPI-I2S/CAN), analog and timers. See
  [`docs/HARDWARE.md`](../../HARDWARE.md) for what has been transcribed from
  it so far.
- **`UM1974-nucleo144-user-manual.pdf`** — ST's own NUCLEO-F429ZI manual. Source
  for the SB121/SB122 solder-bridge documentation in
  [the PA7 conflict](../../explanation/pa7-conflict.md).
- **`ARM-Cortex-M4-processor-datasheet.pdf`** — the core itself: NVIC, SysTick,
  the MPU, exception model. Reference for anything below the HAL that touches
  the processor rather than a peripheral.
- **`AN4013-stm32-timer-overview.pdf`** — ST application note, cross-series
  timer modes. Relevant to the GPIO Swiss-army-knife plan (frequency/duty-cycle
  via input capture, PWM generation) in `docs/ROADMAP.md`.

## A different, older board — do not confuse with the above

- **`TI-C-Board-schematic-OLDER-DIFFERENT-BOARD.pdf`**
- **`TI-C-Board-overview-OLDER-DIFFERENT-BOARD.pdf`**
- **`TI-C-Board-library-api-OLDER-DIFFERENT-BOARD.pdf`**

These describe the lab's *previous* board: an STM32F417ZG with an
EA-eDIPTFT32-a display, SPI2 for the panel, USART1 for the console. Its GPIO
polarity is the **opposite** of this project's board — Port E is input,
Port G is output, versus PD/PE output and PF/PG input here — and it has a
physical microSD/MMC slot on native SDIO pins that **this board does not
have**. A detail copied from these three files without checking which board it
describes will be wrong. Kept for lineage context only; nothing here should be
transcribed into `targets/itsboard/` without cross-checking against
`ITSBRD-schematic-Jaehnichen-HAW-rev02.pdf` first.
