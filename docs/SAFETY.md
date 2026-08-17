# Hardware safety rules

Binding for every change in this repository, and for every agent or human
working on it. The board is a NUCLEO-F429ZI with an ITS adapter and a Waveshare
4" shield stacked on top. Most of it is robust; a handful of things are not, and
those are enumerated here.

The rule of thumb: **when in doubt, do not drive the pin.**

---

## 1. Never touch the debug interface

| Pin | Function | Why |
|---|---|---|
| PA13 | SWDIO | Reconfiguring either pin costs debug access to the board. |
| PA14 | SWCLK | Recovery then needs the BOOT0 jumper and a serial bootloader. |
| PB3 | SWO | Trace output. Left alone so the pin stays available. |

Nothing in this firmware configures GPIOA pins 13/14 or GPIOB pin 3. The HAL
initialises pins one at a time by name rather than writing whole `MODER`
registers, specifically so that a stray port-wide write cannot take SWD out.

## 2. Never touch the clock input

PH0/PH1 carry the 8 MHz clock the ST-Link's MCU feeds in. The PLL is configured
for `HSE_BYPASS`, which means PH0 is an *input*. Configuring it as an output
fights the ST-Link's driver.

Do not raise the clock beyond 180 MHz. Scale 1 plus over-drive plus 5 flash wait
states is the documented maximum for this part at 3.3 V, and it is what
`hal_clock.c` sets.

## 3. Respect pin directions on the ITS adapter

| Pins | Direction | Rule |
|---|---|---|
| PD0..PD7 | output | OUT0..7, LED bank. Safe to drive. |
| PE0..PE7 | output | OUT8..15, LED bank. Safe to drive. |
| PF0..PF7 | **input** | IN0..7. **Never configure as output.** |
| PG0..PG5 | **input** | INT0..5. **Never configure as output.** |

Whatever the adapter has wired to PF/PG may be actively driving those nets. Two
push-pull drivers on one net is how boards die. `hal_io.c` configures them as
pulled-up inputs and never changes that.

## 4. Flash writes are confined to bank 2

| Region | Address | Use |
|---|---|---|
| Firmware | `0x08000000` – `0x080FFFFF` | Bank 1, sectors 0..11. Written only by the flashing tool. |
| Reserved | `0x08100000` – `0x0811FFFF` | Bank 2, sectors 12..16. Left erased. |
| Filesystem | `0x08120000` – `0x081FFFFF` | Bank 2, sectors 17..23. The littlefs volume. |

Rules:

- **No mass erase, ever.** `scripts/flash.sh` uses `st-flash write`, which
  sector-erases only the range being written. A chip erase would take the
  filesystem with it and, on a board configured for it, could touch option bytes.
- The firmware's flash driver refuses any address below `0x08120000`.
  `scripts/flash.sh` additionally refuses an image larger than 1 MB, because such
  an image would run past bank 1 into the filesystem window.
- **Never write option bytes.** Setting read protection (RDP level 1 or 2) is
  either annoying or permanent. Nothing in this repository writes `FLASH_OPTCR`.
- Flash erase/program routines live in `.ramfunc`. The part is dual bank so
  executing from bank 1 while erasing bank 2 is legal, but running the routine
  from RAM removes the question entirely.

## 5. Display and touch

- **The display bus is write-only.** There is no readback path through the
  74HC4094 shift register chain, so the panel can never be queried for its
  state. The RAM framebuffer is the only source of truth about what is on screen.
- **Do not change the ILI9486 power and gamma registers** (`0xC0`, `0xC1`,
  `0xC2`, `0xC5`, `0xE0`, `0xE1`, `0xF1`, `0xF2`, `0xF4`, `0xF8`) without a
  datasheet reason. `VGH`/`VGL` and `VCOM` set panel drive voltages; wrong values
  are one of the few ways software can physically damage a TFT. The table in
  `hal_display.c` is the module vendor's and is reproduced exactly.
- **Raising the SPI clock is a staged experiment, not an edit.** The limit is the
  74HC4094 chain, not the panel. `/16` (5.6 MHz) is proven on this hardware over
  years. Go one divider step at a time, verify visually on the real board, and
  keep the previous value as the fallback. Never go below `/4`.
- The backlight is PWM on PD15 through an S8050 transistor. Any duty from 0 to
  100 % is safe.

## 6. Ethernet and the PA7 conflict

`ETH_RMII_CRS_DV` can only be PA7 on this part, and Arduino D11 (`SPI1_MOSI`,
the display data line) is bonded to PA7 by the board's default strapping. They
cannot both own the pin.

- On a stock board the firmware time-slices: `cads_hal_spi_claim_bus()` stops the
  MAC, waits for in-flight frames to drain, steals the pin, and restores
  everything on release. **Never write to the display without bracketing it in
  claim/release** - doing so corrupts whatever the PHY is receiving.
- Never reconfigure the RMII pins (PA1, PA2, PC1, PA7, PC4, PC5, PG2, PG11,
  PG13, PB13) outside the Ethernet driver.
- The clean fix is the SB121/SB122 swap described in `docs/HARDWARE.md`. That is
  a soldering operation on the Nucleo and is the user's decision, not an
  automated one.

## 7. Test and debugging conduct

- Every interaction with the probe or the serial port runs under a timeout.
  `st-util`, `st-flash`, `gdb` and serial reads can all hang; a test that hangs
  is worse than one that fails.
- Always `pkill st-util` after a debug session. A stale GDB server holds the
  probe and makes the next flash attempt fail in a confusing way.
- `Default_Handler` and `cads_hal_panic()` execute `bkpt #0`. With a debugger
  attached this halts usefully. **Without** a debugger attached a `bkpt` escalates
  to a HardFault, so a panic on an untethered board presents as a lock-up with
  the red LED on - which is the intended, safe failure mode.
