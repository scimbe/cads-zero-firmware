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
- [x] CI workflow: firmware build + size report (.github/workflows/ci.yml,
      commit 05ecb7d) — this was done but never checked off here.

## M1 — Graphics and identity  `[~]`

- [x] Font pipeline: TTF → packed 1 bpp glyph atlas, sizes 12/16/24
      (JetBrains Mono, SIL OFL 1.1; 1 bpp because an indexed canvas cannot
      antialias without burning palette slots on grey ramps)
- [x] `cads_canvas_draw_text` with alignment and clipping
- [x] CaDS lion asset: PNG → 4 bpp indexed C array, brand palette
      (quantisation is lossless for the three brand colours)
- [x] Boot splash: the CaDS mark + "Z E R O" wordmark
- [x] Qualify SPI divider /8 on hardware: 669 kpixel/s, 229 ms full screen,
      1.9x speedup. Not made the default until the panel is confirmed clean
      at the faster clock.
- [x] **HARDWARE GATE M1 PASSED** 2026-08-18: type specimen photographed on
      the panel, all three sizes legible, Leo correctly oriented, 10/10
      assertions still green. The camera caught a horizontal mirror that every
      colour-bar pattern had passed.
- [!] DMA2D (Chrom-ART) path — DEFERRED, with cause. The flush is bus bound:
      342 kpixel/s measured against a 351 kpixel/s theoretical maximum, so the
      bus is 97% saturated and the CPU conversion is not the limit. DMA2D and
      a 16-bit SPI frame both only save CPU cycles, which buys nothing
      measurable today. Revisit when the scheduler lands and those cycles are
      contended.

## M2 — Kernel  `[~]`

- [x] FreeRTOS integration, static allocation only — no kernel heap at all.
      Task stacks in CCM (5 KB of 64 KB used), all DMA-capable SRAM left free.
- [x] `cads_thread`, `cads_mutex`, `cads_queue` — all caller-allocated
- [x] `cads_timer`, `cads_event` — thin FreeRTOS wrappers (xTimerCreateStatic,
      xEventGroupCreateStatic), static allocation, same 80-byte opaque
      storage convention as cads_mutex/cads_queue. VERIFIED on hardware
      under the real scheduler: a one-shot timer's callback (running on the
      FreeRTOS timer service task) sets an event bit observed by the
      console task - 199ms elapsed against a 200ms period, exact marker
      value confirming the callback genuinely ran on a different task.
      Board-only (modules/kernel is not built for the simulator, matching
      its existing scope); a host stub reports so honestly rather than
      silently passing.
- [ ] `cads_pubsub`, `cads_record` (service registry), `cads_string`
- [ ] `cads_log` with levels, routed to the console
- [ ] Fault handlers that dump the stacked frame before halting
- [x] **HARDWARE GATE M2 PASSED** 2026-08-18: 10 minutes under the scheduler
      with forced full-screen redraws, 30 samples, no silent interval, no task
      lost, stack high-water marks converged. Used: ui 224 B of 2048,
      input 132 B of 1024, console 372 B of 2048.

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

## M5 — Network (the hardware advantage)  `[~]`

- [x] PHY management over MDIO — identity, link, speed, duplex. Independent
      of RMII, so it needs neither PA7 nor a solder-bridge decision.
      VERIFIED on hardware: LAN8742A id 0007:C131, model 0x13 rev 1,
      link UP, autoneg done, 100 Mbit full duplex.
- [ ] Bare-metal ETH MAC driver + lwIP netif, DMA descriptors in SRAM
- [ ] DHCP, link state, status bar indicator
- [ ] `cads_cli` over TCP and over the serial console, shared command table
- [ ] Screen streaming: framebuffer to a host viewer over TCP
- [ ] HTTP status page

### The network Swiss-army-knife (verified against DS00001989A, the LAN8742A datasheet)

MDIO-only tools need no RMII data path and no PA7 — same access as the
existing PHY status reader. RMII-dependent tools are gated on the ETH MAC
driver above.

- [x] Cable diagnostics (TDR): open/short detection + distance-to-fault, plus
      matched-cable length estimation on an active link. MDIO-only, safe.
      VERIFIED on hardware.
- [x] Auto-negotiation inspector: decode ANAR/ANLPAR to show what both sides
      offered/agreed (helps diagnose speed/duplex mismatches). MDIO-only.
      VERIFIED on hardware: resolved 100 Mbit full duplex, matching the
      independently-measured SCSR-based reading exactly.
- [x] Link event log: poll register 29 (Interrupt Source Flag, latch-high /
      clear-on-read) to timestamp link-down / remote-fault / auto-neg-
      complete events in a static ring buffer. MDIO-only. VERIFIED on
      hardware: correctly caught the auto-neg-complete event left latched by
      the earlier TDR test, poll semantics confirmed (no duplicate counting,
      no drops).
- [ ] Traffic statistics from the STM32 ETH MAC's built-in MMC counters
      (Tx/Rx packet/byte/error counts) — hardware counters, no software
      counting needed. Needs the MAC clocked but not RMII/lwIP. S.
- [ ] ARP scan of the local subnet. Needs RMII + lwIP. S once M5 lands.
- [ ] Ping / ICMP echo. lwIP ships an example to adapt. Needs RMII. S.
- [ ] Traceroute-style path probe (ICMP TTL sweep). Needs RMII. M.
- [ ] DHCP lease/gateway/DNS display. lwIP's DHCP client exists; needs a
      portable status struct and a UI. Needs RMII. S.
- [ ] iperf-style throughput test via lwIP's lwiperf. Expect well under
      100 Mbit/s — the STM32 ETH has checksum offload but the CPU is still
      the bottleneck on small packets. Needs RMII. M.
- [ ] Configurable-rate packet generator (DMA descriptor ring + timer). Needs
      RMII. M.
- [ ] Promiscuous packet sniffer using the MAC's PM bit in MACFFR. Bottleneck
      is storage/processing at 100 Mbit on an MCU with no OS — frame loss
      under load is likely and must be measured, not assumed. Needs RMII and
      M4 storage. L.
- [ ] MAC address table (switch-style learning with aging) from sniffed
      frames. Needs the sniffer. M.
- [ ] Wake-on-LAN magic-packet sender, independent of the board's own WoL
      support. Needs RMII. S.
- [ ] **HARDWARE GATE M5**: DHCP lease, ping, CLI over telnet, screen
      streaming at a measured frame rate, all with the display active

## M6 — Applications  `[ ]`

- [ ] Desktop with Leo the lion mascot (mood/level state) — compile-checked
      clean for ARM, not yet wired into the build or hardware-verified
- [ ] Main menu, settings, about — compile-checked clean for ARM, not yet
      wired into the build or hardware-verified
- [x] GPIO app driving OUT0..15 and reading IN0..7 / INT0..5 — VERIFIED on
      hardware: the full gui/view + gui/widgets + apps/gpio stack renders
      correctly on the real panel (photographed), input handoff to/from the
      running input task confirmed clean, no task lost, stacks stable. Not
      yet wired into the production task set (apps/bringup/tasks.c) - reached
      via the explorer's `g` command for this first smoke test.
- [ ] Network info app — compile-checked clean for ARM (after fixing a
      real snprintf buffer-size warning), not yet wired into the build
- [ ] A game, to exercise the input and timing paths end to end
- [ ] **HARDWARE GATE M6**: full walkthrough of every app on the board

### The GPIO Swiss-army-knife

All timer-based, all achievable on the existing adapter wiring (PD0-7/PE0-7
outputs, PF0-7 inputs = S0..S7 buttons, PG0-5 general inputs). None of this
needs new hardware.

- [ ] Logic level display: live high/low state of every adapter pin, already
      the core of the `apps/gpio` app.
- [ ] Frequency/period counter on an INT line via timer input capture
      (TIM2/TIM5 are 32-bit general-purpose timers with input capture on
      several AF mappings — confirm exact INT-pin-to-timer-channel mapping
      against ITS-BRD-NucleoPins.xlsx before wiring). S/M.
- [ ] Duty-cycle measurement, same input-capture channel, second capture
      compare register. S/M.
- [ ] PWM generator on an OUT line (any adapter output pin on a timer channel
      can be reconfigured AF instead of GPIO push-pull — verify against the
      pin table which OUT pins have timer AFs; PD/PE pins have mixed timer
      support, some OUT pins may be GPIO-only). S/M, pin-mapping dependent.
- [ ] Simple logic analyzer: sample IN0..7/INT0..5 at a timer-triggered rate
      into a ring buffer in SRAM, render as a waveform on the canvas. Sample
      rate bounded by how fast the canvas can be redrawn (dirty-rectangle
      rule applies) rather than by the GPIO read itself. M.
- [ ] Simple continuity/cable tester using two adapter pins: drive one OUT
      pin, read it back on an IN pin through an external jumper/cable under
      test — same operator-in-the-loop pattern the manufacturer's own
      GPIOTest already uses for the OUT0-to-INTx test. S.

## M7 — Simulator and test pipeline  `[ ]`

- [ ] SDL2 simulator: panel, touch via mouse, adapter I/O panel, console
- [ ] Golden-image tests: render, compare against reference PNGs
- [ ] Unit tests (Unity) for canvas, core, toolbox
- [ ] `scripts/board_test.py` extended: TAP over VCP, per-milestone suites
- [ ] CI: build both targets, unit + golden tests, size regression budget

---

## Open decisions (need the user)

_None outstanding._

## Resolved decisions

- [x] **SB121/SB122 solder bridge — DECIDED 2026-08-18: no modification.**
      The board stays stock, so `CADS_SPI_MOSI_ON_PB5=0` is permanent and the
      display and Ethernet time-slice PA7. Reasoning and consequences in
      docs/explanation/pa7-conflict.md. The flush path already bounds the cost:
      it pushes in 16-row bands and releases the bus between them, so the
      longest uninterrupted receiver blackout is 22.5 ms rather than the whole
      448 ms frame.

## Log

- 2026-08-19 — cads_timer/cads_event added to the kernel module. Caught and
  fixed a real design bug in my own first draft before it reached the board:
  FreeRTOS's timer callback receives only the timer handle, not an arbitrary
  context, so a callback function pointer AND a context pointer cannot both
  travel through it directly - the first draft tried to and would have
  compiled but called through garbage. Fixed by pointing the timer's one
  pvTimerID slot at the owning cads_timer_t itself, which already holds both
  as its own fields. Verified on hardware with a test that proves the
  callback runs on a different task than the caller (a marker value only the
  callback writes) rather than merely proving a flag got set somehow.

  Also hit the same host/board portability mistake as twice before this
  session (explorer_eth.c, tasks.c): explorer.c unconditionally called the
  new test's entry point while its implementation was board-only, breaking
  the simulator link. Fixed with the same pattern already established -
  explorer_kernel_test_sim.c reports "not available" rather than the build
  silently failing or, worse, silently claiming success.

- 2026-08-19 — Two MDIO-only diagnostics merged from `tester`, a peer Claude
  session working in an isolated git worktree (`tester/mdio-diagnostics`
  branch) specifically to avoid the merge-conflict risk of two sessions
  editing one working directory: the auto-negotiation inspector and the link
  event log. Both independently cross-checked their register bit positions
  against ST's own LAN8742 driver before handing the work back; both were
  re-verified here against the primary-source datasheet pages already read
  for the TDR feature (registers 4/5 for ANAR/ANLPAR, register 29 for the
  Interrupt Source Flag) before merging, wiring into the real build, and
  flashing. 8/8 host unit tests pass (2 new: test_eth_aneg, test_eth_linklog,
  both against a scripted fake MDIO bus). Hardware gate 10/10, plus direct
  verification: the auto-neg inspector's resolved mode (100 Mbit full duplex)
  matches the independently-measured SCSR-based reading exactly, and the link
  log correctly caught the auto-neg-complete event still latched from the
  earlier disruptive TDR test.

- 2026-08-19 — A `git add -A` while committing the TDR feature swept in
  unreviewed, in-progress work from two concurrently running subagents
  (modules/storage, all six M6 apps) and pushed it to origin/main without
  review. Nothing broke - neither was wired into any CMakeLists.txt yet, so
  the built and gated firmware was unaffected - but the process was wrong.
  Corrective action: `apps/gpio` and `gui/view`/`gui/widgets` were reviewed
  (individually compile-checked for ARM, one real bug found and fixed - see
  below), wired into the real build, flashed and hardware-verified before
  this commit; `modules/storage` and the other five apps remain compile-
  checked-only pending the same review. Going forward, `git status` is
  checked and files are staged explicitly rather than with `-A`.

  The review caught one real, non-obvious bug: `apps/gpio/cads_gpio.c` used
  `snprintf`, which linking pulled in newlib's heap-init syscall stub
  (`_sbrk`, needing a linker symbol `end` that does not exist - the project
  has no heap by design, see the M2 kernel entry). Fixed by switching to
  `cads/toolbox/fmt.h`, the malloc-free formatter already built for exactly
  this. `apps/netinfo` separately had a real `-Wformat-truncation` warning
  (buffer sized for the realistic case, not the type's theoretical maximum)
  fixed while reviewing.

- 2026-08-18 — Cable diagnostics (TDR) implemented and verified on hardware,
  the first item of the network Swiss-army-knife. MDIO-only per
  DS00001989A section 3.8.9 / Figure 3-16, transcribed exactly rather than
  approximated. Both channels reported MATCHED against the bench's live
  cable, the non-disruptive matched-length read gave a plausible ~6m, and the
  PHY's prior register state (auto-negotiation, Auto-MDIX) was confirmed
  restored after the disruptive TDR portion: link=UP, 100M full, unchanged
  before and after. Distance accuracy against a real known fault is not yet
  verified - that needs a deliberately damaged cable, a bench task for a
  human. Two independent Claude sessions collaborated on the research:
  local reading of the primary-source datasheet resolved the one point a
  peer session's web search could not confirm (whether the LAN8742A does
  real distance-to-fault TDR or only open/short detection - it does the
  former, with a documented formula).

- 2026-08-18 — Ethernet brought up from the end that needs no compromise. The
  PHY's management interface is MDIO on PA2 and MDC on PC1; only the RMII data
  path needs PA7. So the LAN8742A can be identified and its link read with the
  display running normally, and it answers: 100 Mbit full duplex, link up.
  Doing so exposed a latent cost in the SPI arbitration, which keyed off "is
  the ETH clock enabled" and would therefore have stopped and restarted a MAC
  that was not running on every single blit, and handed PA7 to a data path that
  did not exist. It now keys off whether RMII is actually up, which the
  Ethernet driver declares.

- 2026-08-18 — The soak test found a real race on its first meaningful run. An
  idle soak had shown the ui task using 132 bytes of stack, which is the
  measurement of a task that never flushed because nothing was dirty. Driving
  redraws revealed the console task's stack growing instead - because the
  explorer's pattern command was calling cads_canvas_flush() itself, so two
  tasks could flush concurrently and only one of them took the mutex. A flush
  holds the SPI bus for up to 448 ms and reconfigures the Ethernet MAC around
  it; two overlapping would interleave pixel data into the panel. Fixed by
  making the ui task the single flusher and having everything else draw, mark
  dirty and wait. The lesson is the test's, not the code's: a stack measured
  without its deepest call path sizes nothing.

- 2026-08-18 — Tried moving pixel output to a 16-bit SPI data frame so DMA2D
  could feed it directly. Abandoned: it produced wrong colours in BOTH palette
  byte orderings, matching neither model of the byte order, and the
  justification did not survive checking. The flush is bus bound at 97% of the
  theoretical rate, so neither a 16-bit frame nor DMA2D can make it faster -
  they only free CPU cycles that nothing is currently competing for. Reverted
  to the proven 8-bit path and recorded the reasoning in hal_display.c rather
  than leaving a tempting half-finished optimisation behind.

- 2026-08-18 — Two real bugs found, both by measurement rather than reasoning.
  (1) The console dropped characters: the explorer polled the UART every 500 us
  while bytes arrive every 87 us at 115200 baud, and the F4 has no receive
  FIFO. "b 90" arrived as "b", the argument parsed as zero, and the backlight
  was set to 0% while still acknowledging success - a display that looked
  broken and was not. RX is now interrupt driven with a ring buffer.
  (2) The panel was mirrored horizontally. MADCTL needed MY, not MX: with MV
  set the two swap their apparent effect. Invisible to every colour-bar test
  pattern because those are symmetric in X, and only exposed once text was
  rendered and photographed.

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
