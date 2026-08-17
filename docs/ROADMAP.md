# CaDS Zero — Roadmap

Living working document. The 5-minute driver loop reads this file, picks the
first unfinished task, does it, and updates the status here.

**Status legend:** `[ ]` open · `[~]` in progress · `[x]` done · `[!]` blocked (needs a decision from the user)

---

## Ground rules

1. **Every milestone ends with a hardware gate.** No milestone counts as done
   until `scripts/board_test.py` passes against the real ITSboard over the
   ST-Link (`066FFF565282494867161033`).
2. **Never risk the hardware.** `docs/SAFETY.md` is binding. When in doubt,
   do not drive the pin.
3. **Simulator and firmware share everything above the HAL.** A feature that
   only builds for one of them is not finished.
4. **Clean-room.** No code copied from flipperzero-firmware. The concepts are
   fair game, the source is not. Prefix is `cads_`, the mascot is Leo the lion.

---

## M0 — Bring-up  `[x]` (gate passed 2026-08-17)

Prove the toolchain, the boot path and the display path on real silicon.

- [x] Repo, submodules, CMake + arm-none-eabi-gcc toolchain file
- [x] Linker script with the flash/RAM/CCM split and the littlefs window
- [x] Generated vector table (`scripts/gen_vectors.py`)
- [x] Clock tree: HSE bypass 8 MHz → 180 MHz, over-drive, 5 WS
- [x] GPIO, SysTick + DWT time base
- [x] Console on USART3 → ST-Link VCP
- [x] SPI1 with DMA and per-blit Ethernet arbitration
- [x] ILI9486 init and DMA blit
- [x] XPT2046 touch with median filtering
- [x] 4 bpp indexed canvas, dirty rectangles, RGB565 staging
- [x] Bring-up app: banner, LED, colour bars, timing report
- [x] `scripts/flash.sh` (st-flash) and `scripts/board_test.py` (TAP over VCP)
- [x] **HARDWARE GATE M0 PASSED** 2026-08-17, 9/9 assertions on the real
      board. Measured: full screen 448 233 us = 342 kpixel/s (97 % of the
      theoretical 351 kpx/s at SPI/16), 40x40 dirty rect 4 717 us, SysTick
      exact, DWT within 0.03 %.
- [ ] Visual confirmation of the test pattern by a human (the bus is
      write-only, so no software can check this)
- [ ] CI workflow: firmware build + size report

## M1 — Graphics and identity  `[ ]`

- [ ] DMA2D (Chrom-ART) path for L4→RGB565, software path kept as reference
- [ ] Font pipeline: TTF → packed 4 bpp glyph atlas, three sizes
- [ ] `cads_canvas_draw_text` with alignment and clipping
- [ ] CaDS lion asset: SVG → 4 bpp indexed C array, brand palette
- [ ] Boot splash: lion + "CaDS Zero" wordmark
- [ ] Qualify SPI divider /8 on hardware, keep /16 as the fallback
- [ ] **HARDWARE GATE M1**: splash renders correctly, glyph atlas legible,
      DMA2D output pixel-identical to the software reference

## M2 — Kernel  `[ ]`

- [ ] FreeRTOS integration, task stacks in CCM, heap in SRAM
- [ ] `cads_thread`, `cads_mutex`, `cads_queue`, `cads_timer`, `cads_event`
- [ ] `cads_pubsub`, `cads_record` (service registry), `cads_string`
- [ ] `cads_log` with levels, routed to the console
- [ ] Fault handlers that dump the stacked frame before halting
- [ ] **HARDWARE GATE M2**: multi-task blink + display + touch under the
      scheduler for 10 minutes without a fault

## M3 — Input and GUI framework  `[ ]`

- [x] Input service: S0..S7 (PF0..PF7, active low) + touch, one event stream
      with debounce, repeat and long press
- [ ] On-screen navigation cluster (the d-pad equivalent) as a widget
- [ ] `view`, `view_port`, `view_dispatcher`, `gui` compositor with layers
- [ ] Widgets: menu, submenu, dialog, text box, list, status bar
- [ ] **HARDWARE GATE M3**: navigate a three-level menu by touch, no ghost
      touches over 200 interactions

## M4 — Storage  `[ ]`

- [ ] littlefs on flash bank 2 (`0x08120000`, 896 KB, 7 × 128 KB blocks)
- [ ] Flash driver in `.ramfunc`, erase/program bounded to the FS window
- [ ] `storage` service, path API, config persistence
- [ ] File browser app
- [ ] **HARDWARE GATE M4**: write, power-cycle, read back; verify the
      firmware region is untouched by comparing a flash CRC before and after

## M5 — Network (the hardware advantage)  `[ ]`

- [ ] Bare-metal ETH MAC driver + lwIP netif, DMA descriptors in SRAM
- [ ] DHCP, link state, status bar indicator
- [ ] `cads_cli` over TCP and over the serial console, shared command table
- [ ] Screen streaming: framebuffer to a host viewer over TCP
- [ ] HTTP status page
- [ ] **HARDWARE GATE M5**: DHCP lease, ping, CLI over telnet, screen
      streaming at a measured frame rate, all with the display active

## M6 — Applications  `[ ]`

- [ ] Desktop with Leo the lion mascot (mood/level state)
- [ ] Main menu, settings, about
- [ ] GPIO app driving OUT0..15 and reading IN0..7 / INT0..5
- [ ] Network info app
- [ ] A game, to exercise the input and timing paths end to end
- [ ] **HARDWARE GATE M6**: full walkthrough of every app on the board

## M7 — Simulator and test pipeline  `[ ]`

- [ ] SDL2 simulator: panel, touch via mouse, adapter I/O panel, console
- [ ] Golden-image tests: render, compare against reference PNGs
- [ ] Unit tests (Unity) for canvas, core, toolbox
- [ ] `scripts/board_test.py` extended: TAP over VCP, per-milestone suites
- [ ] CI: build both targets, unit + golden tests, size regression budget

---

## Open decisions (need the user)

- [!] **SB121/SB122 solder bridge.** Swapping them moves Arduino D11 from PA7
      to PB5 and lets the display and Ethernet run concurrently at full speed.
      Without it the two subsystems time-slice PA7 and Ethernet drops frames
      during every redraw. Requires soldering on the Nucleo; reversible.
      Until decided, the firmware builds with `CADS_SPI_MOSI_ON_PB5=0`.

## Log

- 2026-08-18 — Button mapping settled from the manufacturer's own hardware test
  (ITS-BRD/its_brd_tst GPIOTest) and the official pin table, rather than by
  probing: S0..S7 are PF0..PF7 active low, INT0..5 are jumper inputs and not
  buttons, OUT LEDs are active high. Two earlier inferences were wrong and are
  corrected in docs/HARDWARE.md: TP_BUSY is PE9 not PB10, and the "OUT LEDs are
  active low" claim - drawn from a photograph - does not survive contact with
  the vendor's test code.

- 2026-08-17 — Repo created, M0 HAL written, PA7 conflict identified and
  documented, per-blit arbitration implemented as the software-only mitigation.
- 2026-08-17 — M0 hardware gate passed, 9/9 on the real board. Diagnosing the
  silent console cost a detour: `stty -f` on a cu.* device applies settings and
  then closes the descriptor, which resets the line discipline, so every
  "baud rate" tried was really the 9600 default. Baud must be set via termios on
  the same fd that is read from; see scripts/cads_serial.py.
