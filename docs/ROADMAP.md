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

## M2 — Kernel  `[x]`

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
- [x] `cads_pubsub`, `cads_record` (service registry), `cads_string` —
      `cads_string` turned out to already exist: `cads/toolbox/str.h` +
      `cads/toolbox/fmt.h`, built earlier for the explorer console, already
      cover bounded copy/append/parse and no-heap number formatting, just
      never connected to this checkbox. Built `cads_pubsub` (intrusive
      subscription list, caller-owned nodes) and `cads_record` (fixed-
      capacity name -> pointer registry) new, in `modules/toolbox` rather
      than `modules/kernel` - both are plain data structures with no HAL and
      no scheduler dependency, so they belong at the bottom of the graph and
      are portable by construction (unlike cads_timer/cads_event above,
      which genuinely need FreeRTOS). Neither is thread-safe internally,
      documented in both headers: a cross-task caller serializes its own
      access, the same convention `cads_ring_t` already established. Exist
      to close the exact layering gap `docs/reference/module-layout.md`
      warns about - a portable app needing something a board-only driver
      knows, without including a `targets/` header (the mistake already made
      once in `apps/bringup/explorer.c`) - for when M5's "link state, status
      bar indicator" needs it. 12/12 host unit tests
      (`tests/unit/test_pubsub.c`, `test_record.c`, plus the ten pre-
      existing), and VERIFIED on hardware, 2026-08-19: explorer command `r`
      exercises subscribe/publish/unsubscribe and register/duplicate-refused/
      unregister/reuse for real inside the actual firmware image, not just
      the host test binary - PASS.
- [x] `cads_log` with levels, routed to the console — `modules/toolbox`
      (`cads/toolbox/log.h`), four levels in syslog order (Error=0, most
      severe, through Debug=3), one global sink rather than a caller-owned
      instance like `cads_tap_t` - a log line has no natural handle to
      thread through every call site, unlike a TAP run which only ever
      happens once, at boot, single threaded. Not thread-safe internally,
      documented as such, for the same reason `cads_pubsub`/`cads_record`
      are not: `cads_hal_console_write()` is already an unlocked blocking
      byte loop today, and fixing that would mean reaching up to
      `modules/kernel`'s `cads_mutex` from below it in the dependency graph.
      No timestamps - attaching one would need a HAL clock call this module
      has no business making; a caller puts one in the message text if it
      wants one. 13/13 host unit tests (`tests/unit/test_log.c`, 9 new
      cases). VERIFIED on hardware, 2026-08-19: wired into the real boot
      sequence (`apps/bringup/bringup.c`, `cads_log_init()` right after
      `cads_hal_console_init()`), and the console capture from a real flash
      shows `I [boot] console up` as the very first line - not a test
      harness, the actual production boot path. A Debug-level line right
      before `cads_tasks_start()` is silent at the default Info minimum by
      design, proving a suppressed level costs nothing on the wire on every
      single boot rather than something a test has to go looking for.
- [x] Fault handlers that dump the stacked frame before halting —
      `targets/itsboard/startup/fault_handlers.c`, strong definitions that
      override the generated vector table's weak
      `HardFault`/`MemManage`/`BusFault`/`UsageFault` aliases (the generated
      file itself is untouched). The standard Cortex-M technique, not
      anything from flipperzero-firmware: a naked trampoline reads
      `EXC_RETURN` out of `LR` to pick MSP or PSP *before* any C prologue can
      overwrite it, branches to a plain C function with the stack pointer as
      its argument, which dumps R0-R3/R12/LR/PC/xPSR plus `SCB->CFSR`/`HFSR`
      (and `MMFAR`/`BFAR` when their validity bits say they mean something),
      then `bkpt` and spins - same halt-don't-reset philosophy as
      `Default_Handler`, so the evidence survives for an attached debugger.
      `cads_fault_init()` (called from `hal_init.c`, right after the console
      exists) enables the three sub-fault handlers in `SCB->SHCSR` - without
      it every fault still gets caught, but only as an undifferentiated
      HardFault, which is the reset default and defeats the point of having
      four distinct handlers. Writes straight to `cads_hal_console_write()`
      and `cads/toolbox/fmt.h`'s stateless hex formatter, nothing else - a
      fault handler runs with unknown scheduler and stack state, not a place
      to trust `cads_log`'s buffering or a mutex that might itself be the
      thing that faulted. Board-only, no simulator equivalent (there is no
      Cortex-M exception model to port).
      VERIFIED on hardware, 2026-08-19: a new explorer command (`z FAULT`,
      guarded - plain `z` refuses and explains why, since this halts the
      firmware for good) executes `udf #0`, a deliberately-undefined
      instruction whose only job is triggering this exact test. Real
      captured output: `UsageFault`, `CFSR = 0x00010000` (bit 16 =
      UNDEFINSTR, exactly what UDF should set and nothing else), `HFSR =
      0x00000000` (proving this was handled as UsageFault directly, not
      escalated - confirms `cads_fault_init()`'s `SHCSR` enable is really
      working), `MMFAR`/`BFAR` correctly absent since their validity bits
      were not set. Board then produced no further output, consistent with
      a clean halt. Reflashed afterward and reverified: M0's boot self-test
      still 10/10 PASS.
- [x] **HARDWARE GATE M2 PASSED** 2026-08-18: 10 minutes under the scheduler
      with forced full-screen redraws, 30 samples, no silent interval, no task
      lost, stack high-water marks converged. Used: ui 224 B of 2048,
      input 132 B of 1024, console 372 B of 2048. Every checklist item above
      is now also individually hardware-verified as of 2026-08-20
      (`cads_pubsub`/`cads_record`, `cads_log`, the fault handlers), so this
      milestone is complete rather than re-run as one combined soak - each
      addition got its own real-hardware proof at the point it landed, and
      M0's boot self-test stayed 10/10 PASS after every one of them.

## M3 — Input and GUI framework  `[~]`

- [x] Input service: S0..S7 (PF0..PF7, active low) + touch, one event stream
      with debounce, repeat and long press
- [x] On-screen navigation cluster (the d-pad equivalent) as a widget —
      DECIDED AGAINST, not merely deferred. Considered and rejected in favour
      of treating the eight buttons as a labelled soft-key strip; see
      docs/explanation/input-scheme.md "Why not an on-screen D-pad". Building
      one now would contradict a decision already made and documented, not
      complete an open task.
- [x] `view`, `view_port`, `view_dispatcher`, `gui` compositor with layers —
      built as `gui/view` (cads_view.c/.h, cads_view_dispatcher.c/.h,
      cads_gui.c/.h). `view_port` is not a separate type here: a
      `cads_view_t` owns its own damage rectangle directly, which is what
      Flipper's ViewPort exists to add on top of a bare view - folding it in
      was a clean-room simplification, not a gap. The compositor's three
      layers (status bar / content / soft-key strip) are real and load
      bearing (see gui/view/cads_gui.h). Wired onto real hardware and
      photographed, commit df3ceca, 2026-08-18.
- [x] Widgets: menu, submenu, dialog, text box, list, status bar — all six
      exist in `gui/widgets` (cads_menu, cads_dialog, cads_textbox,
      cads_list, cads_statusbar, cads_softkeys). "Submenu" is not a distinct
      widget: apps/menu pushes another `cads_menu` view onto the navigation
      stack, which is what a submenu actually is in this framework - see
      apps/settings, whose "Factory reset" row opens exactly that pattern one
      level deeper as a confirm dialog.
- [~] **HARDWARE GATE M3**: navigate a three-level menu by touch, no ghost
      touches over 200 interactions. Split into what can and cannot be
      verified without a human, same as the M0 gate's visual-confirmation
      item:
      - [x] Ghost-touch soak, real hardware, 2026-08-19: 300 samples of
            `cads_hal_touch_read()` with the panel untouched, 0 false
            positives (`scripts/board_cmd.py q 300`, explorer command `q`,
            `apps/bringup/explorer.c`). Exceeds the 200-interaction bar in
            the gate's own wording.
      - [x] The three-level tree itself now exists and is wired in: desktop
            (apps/desktop) -> menu (apps/menu) -> an app (settings/about/
            gpio/netinfo), with settings going one level deeper still into a
            confirm dialog. Built, flashed, and run live on the real panel
            for 45s combined across two runs with the status bar and
            soft-key strip both enabled (explorer command `d`,
            apps/bringup/explorer_app_demo.c) - 13 frames flushed, 315648
            pixels, no fault, no lost task, explorer still responsive
            afterwards. Photographed: the desktop, Leo, the status bar and
            the caption text all render correctly on real silicon.
      - [ ] Touch navigation itself, by a human. Software can drive input
            events synthetically but that would test the dispatcher, not the
            XPT2046 and the finger pressing it - the actual point of this
            line. 0 navigation transitions were observed in the runs above,
            which is the expected and correct result of nobody having
            touched the panel, not a failure. Same category as M0's open
            "visual confirmation ... by a human" item.

## M4 — Storage  `[~]`

- [x] littlefs on flash bank 2 (`0x08120000`, 896 KB, 7 × 128 KB blocks) —
      `modules/storage`, littlefs as a git submodule (`lib/littlefs`),
      wired in as its own `cads_littlefs` library so its vendored warnings
      and its `-Wshadow` don't need fixing (same reasoning as `lib/Unity`).
      `LFS_NO_MALLOC` + `LFS_NO_ASSERT` + `LFS_NO_{DEBUG,WARN,ERROR}` keep it
      inside the no-heap, no-printf rules every other module here follows -
      `LFS_NO_ASSERT` was a real, hard-won fix: without it, littlefs's
      default `LFS_ASSERT(test)` expands to the real libc `assert()`, which
      pulls in the fprintf/stdio chain and therefore `_sbrk`, and the board
      build failed to *link* (not compile) over the undefined `end` symbol
      this project's linker script leaves out on purpose. Invisible on the
      host, which links a real libc with a real heap.
- [x] Flash driver, erase/program bounded to the FS window —
      `modules/storage/src/cads_flash_stm32f4.c`, register level, two
      independent bounds checks per operation (offset-relative and
      absolute-address), `_Static_assert`s tying the compiled-in window to
      `docs/SAFETY.md` section 4 at build time. **Not** `.ramfunc`, despite
      the roadmap line above having said so - see the real-hardware finding
      below, which changed this after it was built and initially tested
      clean on the host emulation (where there is no RAM-vs-flash execution
      distinction to catch the bug).
- [x] `storage` service, path API, config persistence —
      `modules/storage/src/cads_storage.c` (littlefs glue, POSIX-ish
      open/read/write/seek/stat/rename/remove/mkdir/dir_*, opaque handles
      from fixed pools, no heap) and `cads_kv.c` (flat typed key/value store
      for settings, one file rewritten in one shot rather than a database).
      30 host unit tests across `test_flash.c`/`test_storage.c`/`test_kv.c`
      (real littlefs, not a fake, over the RAM-backed `cads_flash_host.c`
      emulation which enforces the same NOR "program only clears bits"
      contract the real driver does).
- [ ] File browser app
- [x] **HARDWARE GATE M4 PASSED**, 2026-08-20: write, reset (not a
      reflash), read back; firmware-region CRC32 identical before and after
      (`0x58A5B49C`, both within the write run and again after the reset).
      Real protocol, not simulated: explorer command `u`
      (`apps/bringup/explorer_storage_test.c`) formats the volume, writes a
      known 64-byte test file, and prints the CRC32 of everything below
      `CADS_FS_BASE` (bank 1 + the reserved bank-2 gap, docs/SAFETY.md
      section 4) computed fresh each run. Run once: format + write + PASS.
      Reset the board with `st-flash ... reset` (never `write` - that would
      rewrite bank 1, making the comparison meaningless). Run again: mounts
      the existing volume without reformatting, reads the same file back,
      byte-for-byte match, PASS. See the log entry below for the real bug
      this gate caught before it could ever have passed cleanly.

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
- [x] Traffic statistics from the STM32 ETH MAC's built-in MMC counters —
      hardware counters, no software counting needed. Only six counters
      exist in this silicon (RM0090 has no broadcast/multicast/byte
      counters), not the fuller set this line originally assumed; corrected
      after reading the actual register map rather than guessing. VERIFIED
      on hardware: all six read exactly zero, which is the CORRECT result -
      the RMII data path does not exist in this firmware yet (M5, below), so
      the MAC's RX/TX are never enabled and no frames flow for the counters
      to count. The meaningful functional test - counters incrementing under
      real traffic - happens once M5's data path lands.
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

### Newly discovered capability (from the adapter's own schematic, 2026-08-19)

Not yet scoped into tasks — noted here so it is not lost, not because it is
committed. See docs/HARDWARE.md "Capability this board has that the firmware
does not yet use" for the full table and sourcing.

- I2C1 is level-shifted (3V3 and 5V headers) and completely unused. An I2C bus
  scanner would be a natural, cheap addition to the Swiss-army-knife theme.
- A 10-channel timer breakout (TIM1/2/3/8) exists specifically for this kind
  of use and is more appropriate for the GPIO frequency/duty-cycle tool than
  repurposing an OUT/IN pin that already has a job.
- CAN1 (SN65HVD231D transceiver) and RS232 (MAX3232, via USART6) are also
  wired and unused - out of scope for now, but real capability if a future
  need calls for it.
- [ ] Wake-on-LAN magic-packet sender, independent of the board's own WoL
      support. Needs RMII. S.
- [ ] **HARDWARE GATE M5**: DHCP lease, ping, CLI over telnet, screen
      streaming at a measured frame rate, all with the display active

## M6 — Applications  `[~]`

- [x] Desktop with Leo the lion mascot (mood/level state) — wired into the
      build and VERIFIED on hardware, 2026-08-19: photographed live on the
      panel (status bar, Leo's portrait, mood caption all rendering
      correctly). Review before flashing found the same snprintf/heap bug as
      apps/gpio, in all four of desktop/settings/about/netinfo - fixed with
      `cads/toolbox/str.h` + `cads/toolbox/fmt.h` in every case, no new
      module needed.
- [x] Main menu, settings, about — wired into the build (apps/menu,
      apps/settings, apps/about) and flashed; the tree boots and runs stably
      for 45s combined across two runs, no fault. See the M3 hardware-gate
      entry above for what that does and does not verify without a human.
- [x] GPIO app driving OUT0..15 and reading IN0..7 / INT0..5 — VERIFIED on
      hardware: the full gui/view + gui/widgets + apps/gpio stack renders
      correctly on the real panel (photographed), input handoff to/from the
      running input task confirmed clean, no task lost, stacks stable. Now
      also reachable through the production app tree (apps/menu -> GPIO), in
      addition to the explorer's standalone `g` command.
- [x] Network info app — wired into the build (apps/netinfo) and flashed. Its
      link-state fields are honest placeholders (has_network only; link_up,
      speed, IP all fixed until the maintainer exposes PHY status through a
      portable service - see the comment in cads_netinfo.c) because the
      Ethernet data path does not exist yet, only MDIO-based PHY management.
- [ ] A game, to exercise the input and timing paths end to end
- [~] **HARDWARE GATE M6**: full walkthrough of every app on the board. All
      five apps below the menu build, flash, and run without fault; a human
      walkthrough of each one by touch and by button is the same open item
      as the M3 gate's touch-navigation line, not a separate one.

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
- [x] Golden-image tests: render, compare against reference PNGs. Hand-written
      BMP reader + PNG encoder (real zlib DEFLATE via Python stdlib, not
      vendored/hand-rolled compression) rather than a new C image library for
      one debug feature. Exact pixel comparison (the sim renders
      deterministically), diff PNG written on mismatch. Two golden images so
      far: boot splash, bring-up self-test pattern. apps/gpio deliberately
      not captured yet - it postdates this branch's fork point from main, see
      targets/sim/golden/README.md for the recipe to add it.
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

- 2026-08-20 — M4 storage: littlefs on flash bank 2, real hardware gate
  passed (write, reset, read back, firmware CRC identical before and
  after). Reviewed and completed a `modules/storage` skeleton that already
  existed but had never been build-verified (its own `tests/` referenced two
  files, `test_storage.c` and `test_kv.c`, that did not exist yet). Two real
  bugs found and fixed on the way, both invisible on the host and both the
  reason this took several hardware-gate attempts rather than one:
  (1) `cads_flash_host.c` was missing `#include <stdbool.h>` - compiled on
  whatever host toolchain the code was last written against, never actually
  built with this project's own CMake config until now.
  (2) littlefs's default `LFS_ASSERT` expands to libc `assert()`, which
  needs `_sbrk` for its fprintf-based failure message - the board build
  failed to *link*, not compile, over the `end` symbol this project's linker
  script leaves undefined on purpose (no heap, ever). Fixed with
  `LFS_NO_ASSERT`, alongside the `LFS_NO_MALLOC`/`DEBUG`/`WARN`/`ERROR` the
  skeleton already had.
  The real finding, the reason a real hardware gate exists at all: the flash
  erase/program routines were originally placed in `.ramfunc`, on the
  documented reasoning that running from RAM would remove any doubt about
  dual-bank read-while-write safety. On real hardware it did the opposite -
  the identical operation, run from `.ramfunc`, produced a different failure
  every few attempts (a genuine `BusFault` caught cleanly by M2's new fault
  handler with `PC` pointing inside the RAM-resident routine per the linker
  map; a spurious `CADS_FLASH_ERR_IO`; a spurious `CADS_FLASH_ERR_VERIFY`),
  while manually replaying the identical register sequence from flash-
  resident code worked every single time, five for five once switched over
  for real. Moved the routines to run from flash instead - which is what
  "dual bank" actually means is legal here - and every subsequent hardware
  run has been clean. `docs/SAFETY.md` section 4 updated to match; it no
  longer prescribes `.ramfunc` for this. A second, real, unrelated bug
  surfaced along the way and is now fixed too: the ART accelerator's data
  cache (`FLASH_ACR.DCEN`) doesn't know when flash content changes under it,
  so an address read once and then modified could read back stale, cached
  data - including inside this driver's own post-program verify. Every
  erase and program now resets the data cache before returning
  (`cads_flash_reset_data_cache()`, disable-reset-reenable, per RM0090's
  documented sequence). Both faults were only ever reachable on real
  silicon; the host's RAM-backed emulation has no execution-location
  distinction and no cache to go stale, which is exactly why this milestone
  waited for the real gate rather than calling the host tests sufficient.

- 2026-08-20 — Built the fault handlers, closing the last open item in M2 -
  the milestone is now `[x]`. `targets/itsboard/startup/fault_handlers.c`
  provides strong `HardFault`/`MemManage`/`BusFault`/`UsageFault` handlers
  that override the generated vector table's weak aliases without touching
  the generated file. Standard Cortex-M technique (a naked trampoline reads
  `EXC_RETURN`'s stack-pointer bit out of `LR` before any C prologue can
  disturb it, branches to a C function that dumps the frame and
  `CFSR`/`HFSR`, then halts) - textbook, not flipperzero-firmware.
  `cads_fault_init()` enables the three sub-fault handlers in `SCB->SHCSR`,
  without which every fault still gets caught but only as an
  undifferentiated HardFault, the reset default. Verified for real, not
  just linked: a new guarded explorer command (`z FAULT`) executes `udf #0`
  and the captured hardware output shows exactly the right thing - genuine
  `UsageFault` (not escalated to `HardFault`, proving the `SHCSR` enable
  works), `CFSR` bit 16 (`UNDEFINSTR`) set and nothing else, `MMFAR`/`BFAR`
  correctly suppressed since their validity bits were not set. Board went
  silent afterward (a clean halt, not a crash loop); reflashed and
  reverified M0's boot self-test still 10/10 PASS.

- 2026-08-20 — Built `cads_log` (`modules/toolbox`), the last piece of M2's
  bundled service-registry line. One global sink rather than a caller-owned
  instance, unlike everything else in this toolbox - a log line has no
  natural handle to thread through every call site, unlike `cads_tap_t`
  which only ever runs once at boot. Four levels in syslog order; not
  thread-safe internally, same documented reason as `cads_pubsub`/
  `cads_record` (`cads_hal_console_write()` is already an unlocked blocking
  byte loop, and a lock would mean reaching up to `modules/kernel` from
  below it). Wired into the real boot sequence, not just built and left
  unused: `apps/bringup/bringup.c` now calls `cads_log_init()` right after
  `cads_hal_console_init()`, and a real flash shows `I [boot] console up` as
  the literal first line out of the UART. 13/13 host tests, M0's boot
  self-test still 10/10 PASS after flashing. Fault handlers that dump the
  stacked frame before halting are the one line left open in M2 now.

- 2026-08-19 — Built `cads_pubsub` and `cads_record` (`modules/toolbox`),
  closing M2's service-registry line. Placed in the toolbox rather than the
  kernel module on purpose: both are caller-owned-storage data structures
  with no HAL and no FreeRTOS dependency, so they build for host and board
  alike without a `_sim.c` stub, unlike `cads_timer`/`cads_event` right above
  them which genuinely need the real scheduler. `cads_string` was already
  done under a name nobody had connected to the checkbox - `cads/toolbox/
  str.h` + `fmt.h`, built earlier for the console. Real hardware gate: 12/12
  host tests (two new suites), flashed, M0's boot self-test still 10/10
  PASS, and a new explorer command `r` exercising both modules for real
  inside the firmware image - PASS. Firmware grew 936 B (111572 -> 112508 B,
  10.75% of FLASH_APP) purely from `cads_toolbox_selftest()` actually calling
  the new functions; the object files added nothing to prior builds because
  nothing referenced them and `--gc-sections` dropped them.

- 2026-08-19 — Wired the whole M6 app tree (desktop, menu, settings, about,
  netinfo) into the real build and onto the real board; M3 mostly closed out
  along with it. Three findings, all real:
  (1) The same snprintf/heap-init-syscall bug already fixed once in apps/gpio
  was present in all four remaining apps (desktop, settings, about, netinfo) -
  every subagent that wrote a formatted-text screen reached for it out of
  habit. Fixed the same way: `cads/toolbox/str.h` + `cads/toolbox/fmt.h`,
  no heap, no new module. `cads/toolbox/str.h` already existed and already
  covers what the roadmap's M2 "`cads_string`" line was asking for, just
  under a name nobody had connected to that checkbox.
  (2) apps/about and apps/netinfo were the first *portable* code to actually
  call `cads_hal_board_info()` - the explorer never had - and the simulator
  never had an implementation. Host link failed the moment they were wired
  in. Added one to `targets/sim/hal_sim.c`, honestly: fields the host
  genuinely cannot make true (cpu_hz, flash_bytes, ram_bytes,
  display_pixels_per_second) report 0 rather than borrowing the board's
  numbers.
  (3) The roadmap itself had drifted from the repo: M3's "view/view_port/
  dispatcher/compositor" and "widgets" lines, and M6's desktop/menu/settings/
  about lines, were still `[ ]` for work that commit df3ceca had already
  built and hardware-verified on 2026-08-18. Corrected rather than re-done -
  see the M3 and M6 sections above for what was actually already true.
  Also settled, while reading gui/widgets/README.md and
  docs/explanation/input-scheme.md to check the above: the "on-screen
  navigation cluster" line was never a gap. It was considered and explicitly
  rejected in favour of the soft-key-strip idiom, already documented, just
  never checked off - fixed to say so rather than to describe a widget this
  project decided against building.
  Real hardware gate, ST-Link 066FFF565282494867161033: built for both
  targets (host: 10/10 unit + golden tests green; itsboard: 111 572 B flash,
  10.64% of FLASH_APP, 58.35% RAM), flashed, M0's boot self-test still 10/10
  PASS. New: ghost-touch soak (explorer `q`, `scripts/board_cmd.py`) - 300
  samples untouched, 0 false positives. New: the full app tree live on the
  panel for 45s combined across two runs (explorer `d`) - 13 frames, 315 648
  pixels, no fault, explorer responsive throughout and after. Photographed:
  Leo, the status bar and the mood caption all correct on real silicon,
  which is also the on-hardware proof that the snprintf rewrite in (1)
  produces correct text rather than just linking. What this cannot close by
  itself: actually navigating the tree by touch needs a human, same as M0's
  open visual-confirmation line - 0 navigation transitions were observed in
  both runs, which is the expected result of nobody touching the panel, not
  a failure.
  Also added `scripts/board_cmd.py`: send one hardware-explorer command,
  capture what comes back, for exactly this kind of one-off on-target check
  (board_test.py only knows the boot self-test; board_soak.py only knows
  long-running load).
  One test-harness observation, not a firmware bug: a full `board_test.py`
  run (flash + listen, no `--no-flash`) intermittently captured a partial
  duplicate of the TAP stream and reported a spurious FAIL, while an
  immediately following `--no-flash` reset-and-listen against the identical,
  already-flashed image passed clean, 10/10, byte-identical timings to the
  "duplicate" portion. Not chased further this session - noting it here so a
  future spurious FAIL after a fresh flash is checked against a bare reset
  before being treated as a regression.

- 2026-08-19 — Closed GitHub issue #18 (the SB121/SB122 solder-bridge
  decision) as stale: the decision was made and logged here on 2026-08-18,
  but `scripts/sync_github.py` never closes issues by design, so nothing had
  told GitHub. Commented with the resolution and closed by hand. The other
  38 open issues at the time were all `[M#]`-pattern roadmap mirrors, already
  accurately reflected - nothing else to triage.

- 2026-08-19 — MAC Management Counters merged from `tester`. Correctly left
  the hardware-gate checkbox at `[~]` rather than `[x]` when handing it back,
  since raw MAC-register access has no host-fakeable seam the way the
  MDIO-based diagnostics do (`hal_eth_mdio.c` has never had one either, for
  the same reason) - exactly the discipline this project asks for. Verified
  here: all six counters read zero, which is the correct answer rather than
  an inconclusive one - this firmware has no RMII data path yet, so the MAC's
  RX/TX are never enabled and nothing increments them. Register offsets
  independently re-checked against the CMSIS `ETH_TypeDef` struct rather than
  taken on `tester`'s word alone, matching the same double-check pattern used
  for the auto-negotiation and link-log merges.

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
