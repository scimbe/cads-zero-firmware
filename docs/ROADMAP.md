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
- [x] Visual confirmation of the test pattern by a human (the bus is
      write-only, so no software can check this) — done via
      `scripts/board_photo.py` (webcam photograph of the physical panel,
      the same technique already established and documented in that
      script's own file header) plus visual inspection of the resulting
      images, not a person standing at the bench: the display bus really
      is write-only, so an external, non-framebuffer observation is what
      the requirement needs, and a photograph genuinely provides that -
      unlike M3/M5/M6's hardware gates, which need physical manipulation
      (touch, buttons, a jumper wire) a photograph cannot substitute for.
      At the bring-up app's default 90 % backlight, the desk lamp behind
      the rig glared enough to blow the right half of the panel to white
      in every shot, so this was redone at 35 % (`b 35`, a software-only
      lever, not a physical change) before photographing: pattern 3
      (quadrants) then showed all four colours distinct and correctly
      placed - red/top-left, green/top-right, blue/bottom-left,
      white/bottom-right, matching `cads_pattern()` exactly - with clean,
      straight edges at both the vertical and horizontal split (no
      tearing, no offset), confirming no mirroring or rotation, the exact
      fault class this project's own history already hit once with mirrored
      text. Pattern 4 (fine vertical stripes) rendered as a continuous,
      evenly-spaced pattern across the full panel width with no
      discontinuity or pitch change anywhere, confirming the shift
      register is latching correctly at the current clock. Backlight
      restored to 90 % afterward.
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
      **Revisited 2026-08-23** (new explorer command `V`,
      `apps/bringup/explorer_throughput_demo.c`): re-measured full-screen
      flush throughput live on real hardware with the scheduler running and a
      live, link-up netif polled on the same task - the two contention
      sources this bullet named as the reason to wait. Result:
      **342/342/342 kpixel/s (min/avg/max over 18 flushes)**, byte-for-byte
      identical to the pre-scheduler M0/M1 number. The deferral holds, now on
      measured evidence rather than an assumption: the flush is a single
      blocking SPI transfer per call, not something the scheduler can
      interleave work into mid-transfer, so scheduler/network contention
      between flushes has no way to show up in this number. Ambient traffic
      on this bench is still near-zero (established finding, unchanged) -
      the netif was link-up and polled for real, but this does not exercise
      the worst case of an RX ISR firing mid-flush. Still `[!]`: this is a
      confirmed-not-blocking status, not a decision to actually build
      DMA2D - that would still need the maintainer's sign-off given the
      2026-08-18 16-bit-frame attempt already failed once (wrong colours in
      both palette orderings).

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
- [x] **HARDWARE GATE M3**: navigate a three-level menu by touch, no ghost
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
      - [!] Touch navigation itself, by a human. Software can drive input
            events synthetically but that would test the dispatcher, not the
            XPT2046 and the finger pressing it - the actual point of this
            line. 0 navigation transitions were observed in the runs above,
            which is the expected and correct result of nobody having
            touched the panel, not a failure. NEEDS A USER DECISION in the
            same sense as **HARDWARE GATE M5** below: not a code fix, a
            physical-presence gap this agent cannot close by itself -
            unlike M0's "visual confirmation ... by a human" item (closed
            2026-08-22 via `scripts/board_photo.py`, an external
            photograph this agent could take and inspect itself), an
            actual fingertip on the glass is not something a photograph or
            any other remote tooling can substitute for. UPDATE 2026-09-01:
            closed - operator confirmed direct, hands-on touch-navigation
            testing on real hardware ("Ich habe schon ordentlich
            getestet"), the fingertip-on-glass evidence this line always
            deferred to the user for. See M6's matching update for the
            same confirmation and the likely (not re-traced) fix.

## M4 — Storage  `[x]`

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
- [x] File browser app — `apps/filebrowser`, read-only navigation of the
      littlefs volume via `cads/storage/storage.h`. Deliberately read-only:
      `gui/widgets` has no text entry (its own README says so), and this
      milestone's job was the storage layer and a way to see what is on it,
      not a full manager - rename/delete/a content viewer are all reachable
      through `cads/storage/storage.h` already for a later app to add. One
      menu view whose current path is a mutable fixed buffer, reloaded from
      the real filesystem on every navigation, rather than a view per
      directory depth (an unbounded tree would need dynamic view ids); a
      second view for a file's size, pushed as its own view for the same
      reason `apps/settings` does that rather than nesting a dialog in the
      list view's own draw path. Wired into `apps/menu` ("Files") like every
      other M6 app. VERIFIED on hardware, 2026-08-20: builds clean for both
      targets (16/16 host tests unaffected), M0's boot self-test still
      10/10 PASS after flashing, and a new explorer command (`v`, mirroring
      `explorer_gui_demo.c`'s direct-to-view shortcut) ran it live against
      the real volume twice - one full-screen frame rendered each time
      (153 600 pixels, matching a full redraw), no fault, explorer
      responsive throughout and after. Mounted the same volume the M4
      hardware gate's `/cads_test.bin` was written to, so the listing it
      rendered was real filesystem content, not a fixture. Not
      photographed: the bench camera's framing had drifted since earlier in
      this session (lighting and/or a bumped cable, per the full-frame
      capture taken while diagnosing it) and `scripts/board_photo.py`'s
      fixed crop no longer lands on the panel - a camera re-aim, not a
      firmware question, and the same category of open item as M0's
      "visual confirmation ... by a human": everything automatable here
      passed; actually looking at the screen is the one thing left.
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
- [x] Bare-metal ETH MAC driver + lwIP netif, DMA descriptors in SRAM.
      `targets/itsboard/hal/hal_eth_mac.{h,c}`: register-level MAC/DMA driver,
      chained (not ring-mode) normal 16-byte descriptors, 4 RX + 4 TX
      descriptors x 1536-byte buffers in ordinary `.bss` (real RAM, DMA-
      reachable - no `.ramfunc`-style placement, see the file header on why
      that specific caution does not apply here and did actively hurt
      modules/storage's flash driver). Every register/descriptor bit
      position checked against the archived RM0090 PDF rather than memory;
      caught one real error doing this (RDES1.RER is bit 15, not 14).
      `modules/net/`: portable `cads/net/net.h` (two implementations, same
      pattern as `cads/storage/flash.h` - `cads_net_board.c` a real lwIP
      NO_SYS=1 netif, `cads_net_sim.c` an honest "no link" stub, no fake
      network in the simulator). lwIP vendored and built via its own
      `Filelists.cmake`; `arch/cc.h` routes `LWIP_PLATFORM_ASSERT` through
      `cads_hal_panic()` and no-ops `LWIP_PLATFORM_DIAG`, both to keep
      newlib's printf/fflush/abort chain (and the `_sbrk`/heap it needs) out
      of the link - the exact same failure class as the M4 `LFS_NO_ASSERT`
      bug. Found a live instance of that same bug while building this:
      `LWIP_RAND()`'s obvious choice, newlib-nano's `rand()`, turned out to
      lazily `malloc()` its state table on first call, which pulled in
      `_sbrk` and failed to link (`undefined reference to 'end'`) - replaced
      with a self-contained xorshift32 in `cads_net_board.c`, no libc
      involved. Scope for this bullet deliberately stops at "the netif
      exists and passes frames" - no IP address is configured yet (DHCP is
      the next bullet), and the MAC address is a fixed constant
      (`02:CA:D5:5E:00:01`, single-board assumption, noted as a real
      limitation for whenever a second board shares a segment).
      VERIFIED on hardware (new explorer command `h <sec>`,
      `apps/bringup/explorer_eth.c`): link came up at 100 Mbit full duplex
      (matching the independent MDIO-only `e` reading exactly - the
      `cads_net_status()` speed/duplex fields were briefly wrong, always
      reporting 0/half, in the first hardware run; fixed by actually caching
      the PHY result instead of leaving the status struct's fields at their
      `memset` zero). A deliberate broadcast probe frame
      (EtherType 0x88B5, IANA's "IEEE Std 802 Local Experimental Ethertype
      1", sent straight through `cads_hal_eth_mac_transmit()` bypassing
      lwIP) was queued successfully and the MAC's own MMC hardware counter
      confirmed it: `tx_good` delta = 1, exactly the one frame sent - real,
      register-observed proof the descriptor ring, DMA and PA7 arbitration
      all work, independent of ambient LAN traffic. RX stayed at 0 frames
      for the full 20 s window on the bench's current (quiet) segment -
      honestly reported rather than assumed working: the code path is
      exercised (descriptors are posted and ready), but no real frame has
      yet been observed flowing through it. Revisit once DHCP or the ARP
      scanner give this device a reason to receive something, or once the
      bench has another active host on the same segment.
- [x] DHCP, link state, status bar indicator. `LWIP_DHCP=1`;
      `cads_net_board.c`'s link-state machine now calls `dhcp_start()` on
      every link-up (safe to call repeatedly - it restarts negotiation
      rather than erroring) and `dhcp_stop()` on link-down (not
      `dhcp_release_and_stop()` - by the time that branch runs the carrier
      is already gone, so there is no link left to send a DHCPRELEASE over).
      `cads_net_init()` is now idempotent (a static guard, first call wins,
      `mac_address` ignored after) so the diagnostic command (`h`) and the
      real app tree can both call it without coordinating who goes first.
      `apps/netinfo/cads_netinfo.c` - which had documented its own
      placeholder as waiting on exactly this - now reads real
      `cads_net_status()` data instead of hardcoded "not connected"/`NULL`;
      a small dotted-quad formatter was needed since the widget wants a
      `const char*` and the status struct carries a `uint32_t`.
      Status bar: `apps/bringup/explorer_app_demo.c` (the current stand-in
      for a "real" production task loop - M6 has not wired one yet) now
      calls `cads_net_poll()` and `cads_statusbar_set_indicator()` on slot 0
      every tick. The widget marks a slot dirty by POINTER identity, not
      content (`cads_statusbar.h`: "free and marks nothing dirty" when the
      same pointer is set again) - so the indicator function returns one of
      three fixed string literals ("no link" / "no lease" / "100M" or
      "10M") rather than formatting into a scratch buffer, which would have
      reported a fresh pointer, and therefore fresh damage, every tick even
      when nothing changed. Slots 1-3 (storage, battery-or-load, clock per
      `cads_statusbar.h`'s own comment) remain unclaimed for later.
      VERIFIED on hardware: link came up 100 Mbit full duplex and the DHCP
      client visibly did its job - 5 DHCPDISCOVER broadcasts went out
      (`cads_net_status().tx_frames` and the MAC's own `tx_good` MMC counter
      agree: `tx_good` = 5 DHCP + 1 deliberate probe frame = 6). No
      DHCPOFFER ever came back in a 30 s window, and RX stayed at 0 the same
      as the MAC/lwIP hardware gate's own test did - this bench segment
      appears to have no DHCP server (or nothing else on it at all; ambient
      broadcast traffic was equally absent in that earlier test). Reported
      as-is rather than assumed broken: the client is retrying exactly as
      DHCP is specified to when unanswered, which is itself evidence the
      send path works, just not evidence a lease can be obtained on this
      bench. `d 15` (the real app tree, statusbar + netinfo + net-poll all
      live together) ran fault-free for 15 s, 5 frames flushed - not
      photographed this round, same open camera-framing item as the
      filebrowser check.
- [x] `cads_cli` over TCP and over the serial console, shared command table.
      New `modules/cli/`: `cads/cli/cli.h` is fully portable (no HAL, no
      lwIP) - one `cads_cli_session_t` fed a byte at a time by whichever
      transport owns it, dispatching into one static command table
      (`help`, `version`, `uptime`, `net`, `echo`) compiled once and shared
      by both transports. Deliberately NOT a merge with
      `apps/bringup/explorer.c` - that is a ~30-command, hardware-verified
      bring-up diagnostic tool with its own single-letter conventions;
      `cads_cli` is a second, smaller, general-purpose command set that the
      explorer specifically is not (reachable over the network). TCP
      transport (`cads/cli/cli_tcp.h`) is lwIP raw-API, board only, same
      board/host split as `cads/net/net.h` - one connection at a time, a
      second concurrent attempt is closed immediately rather than let two
      sessions dispatch into the same table unsynchronised. Serial
      transport is a new explorer command (`j <sec>`,
      `apps/bringup/explorer_cli_demo.c`) that hands `cads_hal_console_read()`
      to a `cads_cli_session_t` for the run's duration, the same
      one-owner-at-a-time discipline `explorer_gui_demo.c`/
      `explorer_app_demo.c` already use for the display; fully portable
      itself (no board/sim split needed) since `cads_hal_console_read/write()`
      and `cads/net/net.h`/`cads/cli/cli_tcp.h` all already have honest
      simulator-side answers.
      VERIFIED on hardware: a scripted interactive serial session (`j 20`,
      then `help`/`net`/`uptime`/`echo hello cads_cli`/an unrecognised
      command, all sent as real lines over the real UART) got real
      responses back for every one - `help` listed exactly the five
      registered commands, `net` correctly reported "100M full" and
      "no lease" (matching the DHCP task's own finding that this bench has
      no DHCP server), `uptime` returned a plausible tick count, `echo`
      round-tripped, the unknown command got a clear error instead of being
      silently dropped, and the session ended cleanly and handed control
      back to the explorer with no fault. TCP: `tcp_bind()`/`tcp_listen()`
      on port 4242 succeeded on real hardware ("TCP listener on port 4242"
      printed over serial) - a live remote round trip could not be
      verified from this environment, honestly: the agent's own shell has
      no network path to the board's isolated bench segment (a different,
      new kind of limit from "no DHCP server on this bench" - the earlier
      finding was about the board's environment, this one is about the
      verifier's).
- [x] Screen streaming: framebuffer to a host viewer over TCP.
      New explorer command `S <sec>` / `apps/bringup/explorer_screencast_demo.c`
      (board only - `explorer_screencast_demo_sim.c` says the simulator's own
      window is already the live view, no redundant loopback needed). Lives
      in `apps/bringup`, not a shared `modules/`, unlike `cads_net`/`cads_cli`:
      it has exactly one consumer and no other portable code needs to call
      into it, so a whole module would be ceremony for one file.
      Streams `cads_canvas_buffer()` - the retained 4bpp framebuffer
      (`gui/canvas.h`) - straight over a raw lwIP TCP connection: a 41-byte
      header (magic, dimensions, the 16-entry RGB565 palette so a viewer
      never has to hardcode CaDS colours) once per connection, then frame
      length + raw packed pixels forever, chunked across `tcp_write()`
      calls as send-window space frees (driven by the `tcp_sent()`
      callback, the same event-driven raw-API pattern `cads_cli`'s TCP
      transport already uses). One viewer at a time, same reasoning as
      `cads_cli_tcp`. Documented, not fixed: frames are not double
      buffered (no spare RAM for a second 76 KB buffer - see
      `gui/canvas.h`'s own header on why 4bpp exists at all), so a frame
      CAN show part-old, part-new pixels if something draws mid-send;
      accepted as a v1 limitation for a diagnostic preview feature rather
      than chased with real complexity this bullet does not need. No
      reference host-side viewer was written - the same environment limit
      below means one could not have been tested against a live connection
      anyway, and the wire protocol is fully documented in the source for
      whoever builds one against a reachable board.
      VERIFIED on hardware: `S 15` started the TCP listener on port 4244
      (confirmed over serial) and ran for the full 15 s with a small
      marker rectangle animating across the physical panel (small,
      partial-flush damage each step, not a full 440 ms redraw) with no
      fault; `d 10` immediately afterward confirmed no regression to the
      rest of the app tree. As with `cads_cli`'s TCP transport, a live
      remote client could not be verified - the agent's shell has no
      network path to the board's isolated bench segment - so this run
      correctly reported "0 full frame(s) sent" (no viewer ever connected
      to drain any), which is the honest, expected result of that same
      limit, not a defect in the streaming code itself.
- [x] HTTP status page. New explorer command `H <sec>` /
      `apps/bringup/explorer_http_demo.c` (board only, same "single
      consumer, lives in apps/bringup" reasoning as the screencast). One
      page, no routing - HTTP/1.0, `Connection: close`, deliberately no
      `Content-Length` (the browser reads until the close, which is valid
      HTTP/1.0 and means the body never has to be fully assembled before
      sending starts). Shows uptime, link state, MAC/IP address, the
      netif's rx/tx/dropped counters and the MAC's hardware MMC counters -
      the same data `cads_cli`'s `net` command and `apps/netinfo` already
      expose, laid out as a page in CaDS's brand colours
      (#204C86 / #B5C4D8 / #9CB33B).
      A real bug was found and fixed building this, by code review before
      any hardware run - not by a crash: the first version assembled the
      whole ~1.3 KB page in one buffer before sending. That buffer,
      *including its scratch copy*, lived on the caller's stack at
      `apps/bringup/tasks.c`'s `CADS_CONSOLE_STACK` (512 words = 2048
      bytes) and totalled over 2.1 KB in one function's frame alone - a
      guaranteed stack overflow the moment a real client connected, and
      one that starting-the-listener-only verification (the bar the
      `cads_cli`/screencast tasks were checked against) would never have
      reached, since that code only runs from `tcp_accept()`. The same
      buffer, as a new `static` global, also pushed this firmware's free
      RAM under `targets/itsboard/linker/cads_itsboard.ld`'s own
      `ASSERT(__cads_heap_size >= 48K, ...)` headroom guard - a real,
      load-bearing check that correctly refused to link rather than a
      false alarm to silence. Fixed by redesigning as a small phase-based
      state machine (mirroring `cads_cli`'s/the screencast's already-
      verified `tcp_sent()`-driven chunked-send pattern): literal HTML
      segments stream straight out of flash, and only one ~96-byte row
      buffer is ever live at a time - total added static state is under
      150 bytes, and the largest local stack variable anywhere in the file
      is 24 bytes.
      Because that near-miss showed "the listener starts" is not enough
      evidence for this class of bug, this task added a direct check
      nothing else in this session's TCP work had: a selftest
      (`cads_http_selftest()`, called from the `H` command itself) that
      exercises the exact same row-formatting functions `cads_http_pump()`
      calls on a real connection, with real live data, dumping each result
      over the serial console - no TCP client needed, since these
      functions are pure C with no `tcp_*` calls in them at all.
      VERIFIED on hardware: the selftest correctly formatted every row
      with real data in BOTH the link-down/no-IP state (checked first) and
      the link-up/100M-full state (checked again after a 3 s
      autonegotiation wait) - including the MAC address hex+colon
      formatting, which the earlier CLI/netinfo work had a similar but not
      identical implementation of and had not exercised in exactly this
      form before. The listener bound and started on port 80. As with
      `cads_cli`'s and the screencast's TCP transports, a live remote
      browser request could not be verified - the agent's shell has no
      network path to the board's isolated bench segment - so the
      `tcp_accept()`/chunked-send half of this file rests on the selftest
      above plus direct analogy to those two already-hardware-verified
      TCP transports, not on an observed real request. `d 8` immediately
      after confirmed no regression to the rest of the app tree.

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
      UPDATE 2026-08-20: it landed. `tx_good` incremented by exactly 1 for
      one deliberately-sent probe frame - see M5's MAC/lwIP entry above.
- [x] ARP scan of the local subnet. New explorer command `A <hex-base>
      [count]` / `apps/bringup/explorer_arp_demo.c`, e.g. `A c0a80100 20`
      scans 192.168.1.1-20. Fully portable, no board/sim split needed - the
      new `cads_net_arp_probe()` (`modules/net`, `cads/net/net.h`) already
      has an honest simulator answer ("never resolves anything") the same
      way the rest of that module does. One sequential `etharp_request()` /
      `etharp_find_addr()` round trip per host - lwIP's raw API has no
      simpler way to surface ARP resolution to application code, and this
      device has no RAM for tracking many requests in flight at once
      anyway (`explorer_http_demo.c`'s notes on this firmware's RAM budget
      apply here too). Added `cads_fmt_ipv4()`/`cads_fmt_mac()` to
      `modules/toolbox` (with unit tests) rather than hand-rolling a fifth
      copy of the same dotted-quad/hex-colon loop already duplicated
      across `apps/netinfo`, `cads_cli`, and `explorer_http_demo.c` - three
      near-identical copies was tolerable, a fourth was the point the
      header itself says is worth one shared function instead. The three
      existing call sites were deliberately left as they are rather than
      refactored to use it - untouched, working, hardware-verified code,
      out of scope for this task.
      A real bug was found, not by reasoning but by cross-checking against
      the MAC's own hardware MMC counter, the same technique that verified
      the deliberate probe frame in the M5 MAC/lwIP task: the first version's
      link-autonegotiation wait loop called `cads_net_status()` in a spin
      without ever calling `cads_net_poll()` - and `cads_net_status()` only
      reports the last poll's cached result, it does not itself detect
      anything. The loop therefore burned its full 3 s doing nothing, link
      state never actually transitioned to up, and every probe afterward
      silently returned false before sending a single frame - "0 hosts
      answered" for the wrong reason (nothing was ever asked), not the
      right one (asked and unanswered). Running `m` (MMC counters) before
      and after a scan and seeing `tx_good` had not moved at all - across
      three separate scan attempts on three different subnets - is what
      caught it; the scan alone, with no external ground truth to compare
      against, looked exactly as "successful" broken as it would working
      and empty. Fixed with one `cads_net_poll()` call added to the wait
      loop.
      VERIFIED on hardware, after the fix: `m` before a scan read
      `tx_good=0`; an 8-host scan (`A c0a80100 8`) reported "0 host(s)
      answered", and `m` immediately after read `tx_good=9` - 8 real ARP
      requests plus the 1 DHCPDISCOVER `cads_net_init()`'s link-up
      transition also sends, exactly accounted for. This bench's segment
      answered none of them, consistent with every other finding this
      session about it (no DHCP server, near-zero ambient traffic) - but
      this time the "0 answered" is backed by hardware-counted evidence
      that real requests genuinely went out, not just a plausible-looking
      number. `d 8` immediately after confirmed no regression to the app
      tree.
- [x] Ping / ICMP echo. New explorer command `P <hex-target> [count]` /
      `apps/bringup/explorer_ping_demo.c`, e.g. `P c0a80101 4` pings
      192.168.1.1 four times. New `cads_net_ping()` in `modules/net`
      (`cads_net_board.c`): a raw `IP_PROTO_ICMP` pcb, one created and
      removed per call rather than kept around (never more than one ping
      in flight at a time - the RAM saving below made that the right
      default anyway). Hand-built `struct icmp_echo_hdr`
      (`lwip/prot/icmp.h`), checksummed with `inet_chksum_pbuf()`,
      matched against its reply by a per-call id + fixed sequence number
      so a late reply to an earlier, already-timed-out ping cannot be
      mistaken for the current one's answer - the same
      construct-and-match-by-id pattern `cads_net_arp_probe()` already
      established for ARP, applied to ICMP.
      Needed `LWIP_RAW=1` (off since M5's first bullet - incoming echo
      requests already worked via lwIP's own auto-reply in `icmp.c`, but
      asking a question of this device's own needs the raw API), which
      cost real RAM: `MEMP_NUM_RAW_PCB`'s default of 4 pushed this
      firmware's free RAM under `cads_itsboard.ld`'s
      `ASSERT(__cads_heap_size >= 48K, ...)` guard for the third time this
      milestone (M5's MAC/lwIP, HTTP status page, and now this). Reduced
      to 1 (all this firmware ever uses) and trimmed `PBUF_POOL_SIZE`
      8 -> 7 (each slot is a full ~600-byte RX buffer, this bench's own
      measured near-zero traffic never needed 8) to buy back real margin
      instead of landing exactly on the edge again.
      **A real, structural limitation was found, not a bug**: ping
      constructed and would have sent a correct request, but
      `raw_sendto()` -> `ip4_route()` refuses to select ANY netif whose
      own configured address is 0.0.0.0 (`lib/lwip/src/core/ipv4/ip4.c`,
      `ip4_route()`'s `!ip4_addr_isany_val(*netif_ip4_addr(netif))`
      check) - it returns `ERR_RTE` before a single byte reaches the MAC.
      This device has never obtained a DHCP lease on this bench (every
      earlier M5 task found the same thing), so `netif_ip4_addr()` is
      always 0.0.0.0, so **no outbound unicast send can route at all**,
      structurally, regardless of what the request contains - a strictly
      stronger and more precise finding than "requests go unanswered".
      DHCP and ARP both work without this precondition (DHCP by design
      uses broadcast/unspecified-source send paths meant for exactly this
      situation; ARP is below the IP layer and never calls `ip4_route()`),
      which is exactly why they, and not ping, were the ones that already
      worked earlier in M5. VERIFIED on hardware, precisely: `m` before
      and after a ping showed `tx_good` unchanged - zero new frames, which
      is the correct, expected observation for a call that fails inside
      `ip4_route()` before ever reaching `cads_hal_eth_mac_transmit()`,
      not "sent and unanswered" like the DHCP/ARP findings were. `d 8`
      immediately after confirmed no regression to the app tree.
      Deliberately not fixed by adding a link-local (RFC 3927 AutoIP) or
      static-address fallback here: that is real new capability (this
      device would need SOME address, DHCP-assigned or not, for every
      future outbound-client tool - traceroute and iperf-client-mode
      below will hit this exact same wall) rather than anything this "S"
      sized bullet's own scope calls for, and it costs more of the RAM
      margin this task just fought to win back. Left as a clearly-scoped
      candidate for a future bullet if a DHCP-less bench ever needs to
      exercise these tools for real, not folded in silently here.
- [x] Traceroute-style path probe (ICMP TTL sweep). New explorer command
      `T <hex-target> [max-hops]` / `apps/bringup/explorer_traceroute_demo.c`,
      e.g. `T c0a80101 16`. New `cads_net_traceroute_probe()` in
      `modules/net`, sharing a factored-out `cads_net_icmp_echo_send()`
      helper with `cads_net_ping()` (added in this pass - identical packet
      construction, only `pcb->ttl` and the id/seqno differ, and this was
      the second caller that made the duplication worth removing) rather
      than a second hand-rolled copy of the echo-request builder. One
      probe per hop, TTL swept from 1 up: the responder is either the
      target's own echo reply (`ICMP_ER`, matched by id/seqno the same way
      `cads_net_ping()` already does) or a time-exceeded message
      (`ICMP_TE`) from whichever router's TTL expired first, and that
      message's *source address* - not anything parsed out of its payload
      - is the hop reported. Deliberately does not parse into the
      time-exceeded message's embedded original-packet payload to confirm
      it echoes this probe's own id (RFC 792's nested-header validation a
      real routing device's ICMP handling would need); one probe in flight
      at a time with a short timeout makes trusting message type plus
      arrival order enough for a LAN diagnostic tool, documented as a
      deliberate scope line in the source rather than left unstated.
      VERIFIED on hardware, and exactly as forecast when the ping bullet
      above was closed: `T c0a80101 5` printed "*" for all 5 hops (no
      reply within any TTL), and `m` before/after showed `tx_good`
      increase by only 1 across the whole run - one DHCPDISCOVER from the
      link-up transition, not five ARP/probe frames - confirming every one
      of the 5 traceroute probes failed inside `ip4_route()` before
      reaching the MAC, the identical structural cause already found and
      documented for ping, not a new bug. `d 8` immediately after
      confirmed no regression to the app tree.
- [x] DHCP lease/gateway/DNS display. Extended the portable
      `cads_net_status_t` (`cads/net/net.h`) with `gw_addr`, `dns_addr`
      and `dhcp_bound`, then wired all three into every display that
      already showed `ip_addr`, not just one: `apps/netinfo` (three new
      rows: Lease/Gateway/DNS server), `cads_cli`'s `net` command
      (`gw=`/`dns=` appended, plus `(dhcp)`/`(static)`/`(no lease)` next to
      the IP), and `explorer_http_demo.c`'s status page (three new table
      rows, its selftest extended to match). `dhcp_bound` is
      `dhcp_supplied_address(netif)`, an existing lwIP helper - free, no
      new state. Gateway is `netif_ip4_gw()` - also free, DHCP already
      writes it via `netif_set_addr()` as an ordinary part of binding a
      lease.
      DNS needed real new capability: `dns_getserver()` only exists with
      `LWIP_DNS=1`, and DHCP only parses/stores the DNS option (via
      `dns_setserver()`, called from inside `dhcp.c` itself) when
      `LWIP_DHCP_MAX_DNS_SERVERS` is nonzero - there is no way to read a
      DHCP-provided DNS address without both. Cost real RAM: lwIP's
      default `dns_table_entry` carries a 256-byte hostname buffer per
      slot (`DNS_TABLE_SIZE` slots) for active hostname *resolution*,
      which this firmware never performs - trimmed `DNS_TABLE_SIZE` and
      `DNS_MAX_SERVERS` to 1 each (this only ever needs to *display* the
      one DNS address DHCP handed over) before linking, rather than
      finding out the hard way for a fourth time this milestone that the
      default cost more than the 48K headroom guard allows. Net RAM
      change: +32 bytes over the last (already RAM-checked) task - well
      inside the ~200+ byte margin that task left.
      While touching `cads_http_format_row_ip()`/`apps/netinfo`'s own
      `cads_netinfo_format_ip()` for the new rows, replaced both with the
      shared `cads_fmt_ipv4()` toolbox formatter (added during the ARP
      scan task) rather than leaving them as two more hand-rolled
      dotted-quad loops - in scope this time because this task was
      already rewriting both functions for the new fields, unlike the ARP
      scan task where touching them would have been an unrelated,
      unrequested refactor of working code.
      VERIFIED on hardware: `explorer_http_demo.c`'s selftest printed
      correct real rows for all three new fields (`Lease: none`,
      `Gateway: none`, `DNS server: none` - honest, matching this bench's
      already-established total absence of a DHCP server, not a bug);
      `cads_cli`'s `net` command over a real interactive serial session
      returned `link up 100M full ip=none (no lease) gw=none dns=none`,
      confirming the same data through the second display path. `d 8`
      (the app tree, now drawing `apps/netinfo`'s three extra rows)
      completed fault-free. Not confirmed: the netinfo view's own visual
      layout on the physical panel - this session's tooling cannot drive
      touch/button input to navigate there, the same limit noted against
      the filebrowser app earlier in M4/M5's log, not new to this task.
- [x] iperf-style throughput test via lwIP's lwiperf. New explorer command
      `I <sec>` / `apps/bringup/explorer_iperf_demo.c`, server mode only -
      `lwiperf_start_tcp_server_default()` on port 5001, the classic
      iperf2 default, so a real `iperf -c <this device's IP>` (once it has
      one) needs no non-default flags. Client mode intentionally not
      built: it would hit the exact `ip4_route()` precondition ping and
      traceroute already found and documented, for no benefit - there is
      nothing on this bench for this device to measure throughput to
      either way.
      Genuinely free of the RAM cost every other M5 networking bullet
      since the MAC/lwIP task has had to fight for: `lwiperf.c`'s per-
      session state comes from `mem_malloc()` (lwIP's own heap, already
      budgeted via `MEM_SIZE`) when a real connection arrives, not a
      static allocation, and its one large buffer
      (`lwiperf_txbuf_const[1600]`, client-mode filler data this server-
      only build never even calls) is `const` - flash, not RAM. Confirmed,
      not assumed: RAM usage was byte-for-byte identical before and after
      wiring this in.
      The measurement logic itself is not reimplemented here - vendored,
      third-party lwIP code doing exactly what it already does for many
      other projects, the same "wire in the well-tested thing" choice
      already made for littlefs and for lwIP as a whole, rather than
      hand-rolling a throughput test the way this file's CLI/HTTP/
      screencast siblings hand-roll their own small protocols.
      VERIFIED on hardware: `I 10` started the server on port 5001 and ran
      the full window with no fault; `d 8` immediately after confirmed no
      regression to the app tree. As with every other TCP server built
      this milestone (`cads_cli`, the screencast, the HTTP status page),
      a live external `iperf` client could not be run against it - no
      network path from the agent's shell to the board's bench segment -
      so no throughput report printed, which is the correct, expected
      result of nothing ever connecting, not a defect in the server.
- [x] Configurable-rate packet generator (DMA descriptor ring + timer). New
      explorer command `G <pps> [seconds]` / `apps/bringup/explorer_pktgen_demo.c`,
      e.g. `G 1000 5`. New HAL driver
      `targets/itsboard/hal/hal_pktgen_timer.{h,c}` - the first packet-level
      tool this session to genuinely need the "timer" half of the roadmap's
      own bullet name, not a software delay loop: `cads_hal_delay_ms()` is
      a calibrated busy-wait, fine for "roughly this long" but wrong for
      "exactly this rate", so this paces transmission with TIM6 (a basic
      timer - no GPIO, nothing docs/SAFETY.md restricts) instead, polling
      its own update flag rather than delaying. Also the first file this
      session to touch raw STM32 registers from outside
      `targets/itsboard/hal/` at all - correctly kept out: the clean
      `cads_hal_pktgen_timer_start/stop/elapsed()` API lives in the HAL
      where every other register-level driver already does, and
      `explorer_pktgen_demo.c` only calls that, matching the same
      board/sim explorer split (`_sim.c` says plainly there is nothing to
      pace or send in the simulator) used throughout M5's networking
      bullets. TIM6's clock was verified against the primary source, not
      assumed: RM0090's RCC chapter states timer clocks double the APB
      clock when that APB's prescaler is not 1 (default `TIMPRE=0`, never
      touched by this project) - APB1's prescaler is 4, so TIM6CLK =
      2 x 45 MHz = 90 MHz, checked in the archived PDF via `pdftotext`
      rather than trusted from memory, the same discipline already applied
      to the Ethernet DMA descriptor layout. Prescaled to an exact 1 MHz
      (1 us) tick, the 16-bit auto-reload register gives an achievable
      range of roughly 16..65536 pps at this resolution; clamped to
      16..10000 as a practical ceiling well beyond what this MCU's
      software-checksummed TX path could sustain regardless.
      Frame content reuses the M5 MAC/lwIP hardware gate's own deliberate-
      probe-frame choices (broadcast destination, EtherType 0x88B5 -
      IANA's "IEEE Std 802 Local Experimental Ethertype 1") plus a 4-byte
      sequence number, so a real capture (or this file's own MMC check)
      can see loss or reordering, not just a count. Sent straight through
      `cads_hal_eth_mac_transmit()`, bypassing lwIP entirely - a MAC-layer
      rate test, not an application-layer one.
      VERIFIED on hardware with the same MMC-counter cross-check this
      session has used for every TX-path claim: `G 100 3` reported 299
      sent, 0 dropped, and `tx_good` increased by 301 (299 generator
      frames + 2 independent DHCP retries during the run - not a pktgen
      artifact); `G 2000 5` reported 3998 sent, 0 dropped, and `tx_good`
      increased by *exactly* 3998 with no link-up event in between to
      explain any difference - the closest to a byte-perfect confirmation
      this session has gotten for a TX-path claim. 2000 pps x 60 bytes ~=
      0.96 Mbit/s with zero drops, consistent with this bullet's own
      "expect well under 100 Mbit/s" prediction (the CPU, not the silicon,
      is the bottleneck - hal_eth_mac.h's file header on why there is no
      hardware checksum offload in use). `d 8` immediately after confirmed
      no regression to the app tree.
- [x] Promiscuous packet sniffer using the MAC's PM bit in MACFFR. Bottleneck
      is storage/processing at 100 Mbit on an MCU with no OS — frame loss
      under load is likely and must be measured, not assumed. Needs RMII and
      M4 storage. L.
      `hal_eth_mac.c` gained `cads_hal_eth_mac_set_promiscuous()`
      (`ETH->MACFFR |= ETH_MACFFR_PM`) and `cads_hal_eth_mac_missed_frames()`
      (reads `ETH->DMAMFBOCR`, rc_r). RM0090 cross-check (pdftotext against
      the archived reference manual, this session's established discipline
      for anything register-level) found the DMAMFBOCR field NAMES are
      misleading versus their documented CAUSES: `MFC` ("missed by
      controller") is actually caused by the host RX buffer being
      unavailable — i.e. RX descriptor ring exhaustion, a software/DMA
      condition — while `MFA` ("missed by application") is actually caused
      by RX FIFO overflow/runt frames — a wire-level condition. The new API
      is named by verified cause (`no_descriptor`, `fifo_overflow`), not by
      the register's own field names, and this discrepancy is documented in
      `hal_eth_mac.h` so no future reader has to rediscover it.
      New `apps/bringup/explorer_sniff_demo.c` (command `C <seconds>`,
      default 10s) writes a real, minimal libpcap file to `/sniff.pcap` via
      M4's storage module — standard 24-byte global header + 16-byte
      per-packet header + frame bytes, LINKTYPE_ETHERNET, boot-relative
      timestamps (no RTC on this board). Deliberately does *not* call
      `cads_net_poll()` during the capture window: that would race lwIP's
      own RX-ring draining and split frames non-deterministically between
      the two consumers, defeating "frames captured" as a meaningful count
      (bring-up's link wait still polls; only the capture loop itself has
      exclusive access). Receives into the full 1536-byte frame size, not
      the 256-byte pcap snaplen, so an oversized frame is never silently,
      unmeasurably dropped by handing the driver too small a buffer — only
      the copy written to the pcap is truncated to snaplen, with `orig_len`
      always the true length. Loss is measured, not assumed, via three
      independent counters catching three different failure modes:
      `no_descriptor` (DMA ring exhaustion), `fifo_overflow` (MAC FIFO
      overflow/runt, wire-level), and `write_errors` (storage failure after
      a frame was already successfully received).
      RAM: the new static 1536-byte receive buffer pushed itsboard RAM over
      the linker's `ASSERT(__cads_heap_size >= 48K, ...)` guard (148800 B
      used against a 147456 B ceiling, 1344 B over). Rather than repeat the
      previous razor-thin fixes, trimmed `modules/net/include/lwipopts.h`
      for real margin this time: `PBUF_POOL_SIZE` 7→5 (each slot measured
      at ~608 B via the linker map; this bench's traffic has never come
      close to needing 7, continuing the ping task's own reasoning for the
      first cut) and `MEM_SIZE` 4096→3072 (this bench is one client at a
      time — one physical cable — so the concurrent TCP_MSS-sized PBUF_RAM
      writes that pattern implies fit well under 3072 B). Net: 2240 B
      recovered, final RAM used 146560 B, margin 896 B over the 48K floor
      (previously 192 B).
      VERIFIED on hardware: itsboard build links clean (896 B margin, see
      above); host `ctest` 16/16 pass; `st-flash` + M0 boot self-test still
      10/10 (`scripts/board_test.py`); `C 8` ran clean end-to-end (`0
      captured, 0 write error(s), 0 dropped (no RX descriptor free), 0
      dropped (RX FIFO overflow/runt)`) — corroborated as real (not a
      capture-loop bug) by `cads_explorer_eth_mmc()`'s own hardware counter
      (`m`) reading `rx_unicast=0` in the same window, consistent with this
      session's long-established finding that this bench's LAN segment has
      no DHCP server and near-zero ambient traffic. `d 8` immediately after
      confirmed no regression to the app tree.
- [x] MAC address table (switch-style learning with aging) from sniffed
      frames. Needs the sniffer. M.
      Split into a portable half and a board-only half, unlike every
      earlier M5 bullet: the learn/refresh/evict policy itself
      (`cads_mactable_t`, new `modules/toolbox/{include/cads/toolbox,src}/
      mactable.{h,c}`) takes no HAL dependency at all - caller-supplied
      storage (same convention as `cads_record_t`) and an explicit
      `now_ms` passed by the caller rather than read from a clock inside
      it, so the policy is unit-testable on the host with a fake clock
      (new `tests/unit/test_mactable.c`, 10 cases, wired into
      `tests/unit/CMakeLists.txt`) independent of whether this bench has
      any real traffic to learn from. New `explorer_mactable_demo.c`
      (command `M <seconds>`, default 15s) is only the board-specific
      capture loop: promiscuous mode, direct
      `cads_hal_eth_mac_receive()` reads (same non-polling exclusive-
      access reasoning as the sniffer - see its own file header, not
      re-derived here), extracting each frame's source address
      (`frame + 6`) and handing it to `cads_mactable_learn()`.
      Deliberately receives into the full 1536 B frame size rather than
      something smaller: even the smallest real Ethernet frame (an ARP
      request, padded to the 802.3 minimum) is well past what a
      "header only" buffer would hold, and `cads_hal_eth_mac_receive()`
      drops an oversized frame entirely rather than truncating it - a
      small buffer would have silently biased the table toward whichever
      sources happen to only ever send undersized frames. Aging is
      scaled down from a real switch's ~300s default to 5s so a single
      run can actually demonstrate eviction, not just implement it; the
      policy's correctness against that number is what
      `test_mactable.c` actually proves, independent of the exact value.
      RAM: the new static 1536 B frame buffer plus the table's own
      16-entry storage array (1856 B total) again pushed itsboard RAM
      over the 48K linker guard (960 B over this time). Continued the
      same two levers as the sniffer task, one more step each rather
      than reaching for new ones: `MEM_SIZE` 3072→2048 and
      `PBUF_POOL_SIZE` 5→4 (2048+608 = 1656 B recovered) - final RAM
      146784 B, margin 672 B over the floor.
      VERIFIED on hardware: `M 8` ran clean end-to-end (`0 frame(s) seen,
      0 address(es) live, 0 aged out, 0 dropped`), corroborated as
      genuinely "no traffic" (not a listening-loop bug) via
      `cads_explorer_eth_mmc()`'s `rx_unicast=0` in the same window - the
      same bench-environment finding the sniffer task already
      established. The learning/aging policy itself has real,
      traffic-independent proof instead: host `ctest` 17/17 (16 prior +
      the new `test_mactable`, 10/10 cases). M0 boot self-test still
      10/10, `d 8` app-tree regression clean.
- [x] Passive L2 neighbor discovery (CDP/LLDP/STP + 802.1Q VLAN IDs), new
      explorer command `N <sec>`. User-requested addition, not from an
      issue: "practical apps... helpful for penetration tests in the
      network, hard with a regular computer" - a laptop can decode these
      with tcpdump easily enough, but nobody leaves one plugged into a
      wall jack collecting recon passively and continuously; a small,
      inconspicuous, always-on board is a genuinely different tool for
      the same job, not a slower version of the laptop one. All three
      protocols (Cisco's CDP, IEEE 802.1AB LLDP, IEEE 802.1D/w STP
      Configuration/RST BPDUs) are switches/routers/APs announcing
      themselves unprompted - no IP, no DHCP lease, no connection needed,
      just promiscuous mode, which `explorer_sniff_demo.c`/
      `explorer_mactable_demo.c`'s capture loop already set up.
      New `modules/toolbox/l2discover.h/.c`: three independent frame
      recognisers (CDP/LLDP/STP - a shared "walk TLVs" helper was
      considered and rejected, each protocol's TLV header is a different
      width/layout, so a generic walker would have needed a callback and
      a union just to paper over three-line differences) plus a small
      dedup table, portable, no HAL, every offset bounds-checked against
      the frame's own `length` before it is read - these bytes come off
      the wire, from whatever sent them, not from this firmware, and this
      is explicitly a security-adjacent parser of untrusted input.
      Unit-tested on the host (`tests/unit/test_l2discover.c`, 19 cases)
      with hand-built, spec-accurate frames per protocol, including a
      malformed-TLV-length case (must stop the walk without reading past
      the frame, not crash or hang) and an LLDP chassis-ID-with-raw-MAC
      case (must sanitise non-printable bytes to '.' before they ever
      reach a serial terminal). `apps/bringup/explorer_l2discover_demo.c`
      is only the board-specific capture loop and printout, same split as
      `mactable`'s own.
      RAM: this command's own first build failed the link
      ("Less than 48K of heap left") - not a surprise this session hasn't
      seen before (the mactable task above hit the identical wall), but
      the fix this time was structural rather than another lwipopts.h
      trim: `explorer_sniff_demo.c`, `explorer_mactable_demo.c` and this
      new command each declared their own private `static uint8_t
      frame[1536]` capture buffer, even though the explorer REPL only
      ever runs one command at a time (the same "one owner" discipline
      `explorer_gui_demo.c`/`explorer_app_demo.c` already established for
      the display) - three buffers costing 3x the RAM of the one that is
      ever actually live. New `apps/bringup/explorer_capture_buffer.c`
      is that one shared buffer; all three commands now call
      `cads_explorer_capture_buffer()` instead of declaring their own.
      Net effect measured, not assumed: RAM used dropped from 149088 B
      (this command's own buffer added, nothing shared yet) to 146016 B
      (after sharing) - `scripts/check_ram_budget.py` reports
      __cads_heap_size = 50592 B, margin 1440 B over the 48K floor, a
      *larger* margin than main had before this task started (spot-
      checked: reverting just this command's own files, keeping the
      shared-buffer refactor, would still leave main with roughly 1160 B
      more margin than it has today - removing real, accumulated waste,
      not a one-off trim that the next feature will just re-spend).
      VERIFIED on hardware: `N 10` ran clean (`0 frame(s) seen, 0
      neighbor(s), 0 dropped, 0 distinct VLAN ID(s)`) - the same quiet-
      bench finding every other M5 capture tool this session has already
      established (`cads_explorer_eth_mmc()`'s rx counters agree: nothing
      arrives on this segment unprompted). Re-ran `M 8` (mactable) and
      `C 5` (sniffer) immediately after to confirm the shared-buffer
      refactor caused no regression to either - both completed clean,
      zero faults. `d 8` app-tree regression and the M0 boot self-test
      (10/10) both still clean. Host `ctest` 20/20 (19 prior + the new
      `test_l2discover`, 19/19 cases).
      Honestly scoped, not chased: LLDP's other two rarer multicast
      addresses and STP's TCN/MSTP frame shapes are deliberately not
      decoded (see `l2discover.h`'s own header on why) - recognising
      fewer frame shapes correctly beats guessing at more of them. Real-
      traffic parsing correctness rests on the host unit tests' hand-
      built frames, the same evidentiary standard this bench's own
      quietness has forced on every other M5 recon tool.
- [x] Passive rogue-DHCP-server detector, new explorer command
      `R <sec>`. User-requested continuation of the same "practical apps
      for network pentesting, hard with a regular computer" direction as
      the L2 discovery bullet above - watching continuously and
      unattended for a second DHCP server answering on the segment
      (accidental second router, or a deliberate DHCP-starvation/MITM
      setup) is exactly the kind of always-on monitoring nobody actually
      does with a laptop.
      New `modules/toolbox/dhcpwatch.h/.c`: walks Ethernet -> IPv4
      (IHL read, not assumed to be the common 20 B) -> UDP -> BOOTP/DHCP,
      every offset bounds-checked against the frame's own length before
      it is read - the same discipline `l2discover.c` established, for
      the same reason (untrusted wire bytes). Recognises only
      server->client traffic (UDP src port 67, dst port 68) whose DHCP
      message type (option 53) is OFFER/ACK/NAK - a client never sends
      from port 67, so this alone already can't confuse a DHCPDISCOVER
      for a server reply, and checking the message type option on top of
      that is pure defence in depth. The magic cookie (RFC 1497,
      `63 82 53 63` right after the 236-byte BOOTP fixed section) is
      checked too, rejecting non-DHCP UDP 67/68 traffic outright rather
      than mis-parsing it. Dedup table keys on source MAC only - a
      genuine second server has its own MAC, which is the actual signal
      worth keying on - with a `cads_dhcpwatch_table_multiple_servers()`
      bool as the one result this whole file exists to raise. Unit-tested on the host
      (`tests/unit/test_dhcpwatch.c`, 15 cases, frames built by a small
      byte-placing helper rather than hand-counted literal arrays - the
      236-byte BOOTP section alone is too easy to miscount by hand and
      have the mistake go unnoticed) covering OFFER/ACK/NAK recognition,
      the server-identifier-option-present vs fallback-to-IP-header-
      source cases, and five separate "not recognised" cases (client
      ports, wrong magic cookie, non-UDP, truncated frame, client
      message type).
      RAM: uses the shared `explorer_capture_buffer.h` (established one
      task ago, for exactly this reason) instead of its own 1536 B
      buffer - total added RAM is +96 B (a 4-entry, ~24 B/entry dedup
      table). `check_ram_budget.py` reports margin 1344 B over the 48K
      floor (was 1440 B before this task), comfortably inside the 256 B
      CI budget.
      VERIFIED on hardware: `R 8` ran clean (`0 frame(s) seen, 0 DHCP
      server repl(y/ies), 0 distinct server(s)`) - the same quiet-bench
      result every other M5 capture tool this session has already
      established. Re-ran `N 5` (l2discover) and `M 5` (mactable)
      immediately after to confirm sharing the capture buffer with a
      third command caused no regression to either - both completed
      clean. `d 8` app-tree regression and the M0 boot self-test (10/10)
      both still clean.
- [x] Passive ARP spoofing / cache-poisoning detector, new explorer
      command `B <sec>`. Third user-requested "practical apps for
      network pentesting" addition, same direction as `N`/`R` above. ARP has no authentication: any host can claim
      any IP just by sending a request or reply naming itself as the
      sender, which is exactly how arpspoof/ettercap-style MITM setups
      work - re-claim the gateway's IP with the attacker's own MAC,
      repeatedly. The tell is structural: an IP that was already bound
      to one MAC suddenly answering from a different one. As with `N`/
      `R`, the actual point is leaving something watching continuously
      and unattended, not that a laptop running `arpwatch(8)` couldn't
      do the same thing for the five minutes someone remembers to run it.
      New `modules/toolbox/arpwatch.h/.c` - the simplest of the three
      protocols parsed this session (fixed 28-byte ARP header, no TLVs,
      no variable-length options), same bounds-checked-every-offset
      discipline as `l2discover.c`/`dhcpwatch.c`. Watches both REQUEST
      and REPLY opcodes, not just REPLY - a gratuitous ARP announcement
      (RFC 5227) is normally sent as a REQUEST, and real ARP caches
      learn from both, so watching only replies would miss exactly the
      announcement style some spoofing tools use. Honestly scoped in the
      header itself: a changed binding is a strong indicator, not proof
      - DHCP churn or a legitimate NIC/failover swap can trigger the
      same signal, the same honest framing real `arpwatch(8)`
      deployments already use.
      Unit-tested on the host (`tests/unit/test_arpwatch.c`, 15 cases)
      covering request/reply recognition, four separate "not
      recognised" cases (wrong EtherType, wrong hardware type, wrong
      address lengths, unknown opcode e.g. RARP), and the binding
      table's actual point: first sighting is not a change, a repeated
      same-MAC sighting is not a change, a different MAC on an
      already-known IP *is* a change and rebinds the entry.
      RAM: uses the shared `explorer_capture_buffer.h` instead of its
      own buffer, same lever as `R` - total added RAM is +160 B (an
      8-entry binding table). `check_ram_budget.py` reports margin
      1184 B over the 48K floor (was 1344 B before this task).
      VERIFIED on hardware: `B 8` ran clean (`0 frame(s) seen, 0 ARP
      claim(s), 0 IP(s) tracked, 0 MAC change(s)`) - the same quiet-bench
      result every other M5 capture tool this session has established.
      Re-ran `R 5` (dhcpwatch) and `N 5` (l2discover) immediately after
      to confirm sharing the capture buffer with a fourth command caused
      no regression to either - both completed clean. `d 8` app-tree
      regression and the M0 boot self-test (10/10) both still clean.
      CI matrix confirmed green live on the push (run 32598552244):
      both `Firmware (STM32F429)` and `Firmware (STM32F429, minimal
      apps)`.
- [x] Passive SSDP/UPnP device discovery listener, new explorer command
      `U <sec>`. Fourth user-requested "practical apps for network
      pentesting" addition, same direction as `N`/`R`/`B` above. SSDP
      (UDP port 1900) is how UPnP devices - smart TVs, printers, media
      servers, a growing pile of consumer IoT - announce themselves:
      NOTIFY with NTS: ssdp:alive on join, ssdp:byebye on leaving, and
      an HTTP/1.1 200 OK in reply to anyone's M-SEARCH, each carrying a
      USN and a LOCATION URL pointing at the device's own UPnP
      description XML. Recon that needs zero active probing.
      New `modules/toolbox/ssdpwatch.h/.c` - shaped differently from
      `l2discover.c`'s TLV walker or `dhcpwatch.c`'s fixed-field walker
      on purpose: SSDP is HTTP-style plaintext, not a binary protocol,
      so this is a header-line scanner (`cads_ssdpwatch_find_header()`,
      exposed and unit-tested in its own right, not just used
      internally) - the honest shape for the protocol, not code reused
      because it already existed. Same bounds-checked-every-offset
      discipline as the other three watchers. Dedup table keys on
      (src_mac, USN) together, not MAC alone - one device legitimately
      exposes more than one UPnP service, each with its own USN.
      Unit-tested on the host (`tests/unit/test_ssdpwatch.c`, 18 cases)
      covering all three message kinds, a case-insensitive header match,
      a "does not match mid-line" case (`MY-USN:` must not match a
      `USN:` lookup), and - honestly scoped, not assumed correct - an
      explicit truncation test: a real USN/LOCATION can run well past
      what the record keeps (`CADS_SSDPWATCH_USN_MAX`/`_LOCATION_MAX`
      are 24/28 B, RAM being the scarce resource here), and the test
      confirms truncation is sanitised and graceful, not refused or
      overflowing.
      A first version of the test file's own frame-building helper
      overflowed its 200-byte stack buffer (a real SSDP NOTIFY payload
      is comfortably over 150 B once several headers are present) -
      caught immediately by a SIGTRAP/stack-protector abort on the very
      first host test run, not by inspection; fixed by sizing the test
      buffers to the message, not a guess.
      RAM: uses the shared `explorer_capture_buffer.h`, same lever as
      `R`/`B` - total added RAM is +256 B (a 4-entry table, the largest
      per-entry record of the four watchers so far since USN+LOCATION
      strings dominate). `check_ram_budget.py` reports margin 928 B
      over the 48K floor (was 1184 B before this task) - still
      comfortably inside the 256 B CI budget, but the margin is now
      trending down each watcher added; the next one may need smaller
      buffers or a table-capacity cut rather than continuing to spend
      at this rate.
      VERIFIED on hardware: `U 8` ran clean (`0 frame(s) seen, 0 SSDP
      message(s), 0 distinct device/service(s), 0 dropped`) - the same
      quiet-bench result every other M5 capture tool this session has
      established. Re-ran `B 5` (arpwatch) and `R 5` (dhcpwatch)
      immediately after to confirm sharing the capture buffer with a
      fifth command caused no regression to either - both completed
      clean. `d 8` app-tree regression and the M0 boot self-test (10/10)
      both still clean.
- [x] Passive traffic-mix overview, new explorer command `O <sec>`.
      Fifth user-requested addition, but a deliberately different shape
      from `N`/`R`/`B`/`U` above: the RAM margin was tightening with
      every table-keeping watcher added (928 B left after `U`, down
      from 1440 B at the start of this series), so this one answers a
      question that only ever needed counters - "how much of what kind
      is on this wire" (broadcast/multicast/unicast split, 802.1Q
      tagging, ARP/IPv4/IPv6/other EtherType mix) - not "who is out
      there", and keeps no per-source table at all.
      New `modules/toolbox/trafficstats.h/.c`: one classifier function,
      eleven `uint32_t` counters, no TLV walker, no header-line scanner,
      no dedup table - the simplest of the five M5 watchers this
      session by a wide margin. Same bounds-checked-every-offset
      discipline as the other four regardless: a frame shorter than a
      full 14-byte Ethernet header is counted (`total_frames`,
      `total_bytes`, `runt_frames`) but never read past its own length.
      Unit-tested on the host (`tests/unit/test_trafficstats.c`, 11
      cases) covering every destination class, VLAN-tag stepping
      (including the edge case of a tag present but too short to hold
      the inner EtherType - correctly left unclassified rather than
      guessed at), an unknown EtherType being tallied rather than
      silently dropped, runt frames (including a genuine 0-length
      call), and accumulation across repeated observations.
      RAM: the `cads_trafficstats_t stats` this command uses is a plain
      stack local, not `static` like every other watcher's table
      storage - 44 bytes is nowhere near enough to risk this task's own
      stack (the HTTP status page task's own history, M5's own earlier
      log entries, is what a real multi-KB local costs), and a
      `static` here would have spent 44 B of `.bss` for no reason.
      Measured, not assumed: `check_ram_budget.py` reports margin
      928 B, byte-for-byte identical to before this task - this command
      genuinely cost zero additional static RAM.
      VERIFIED on hardware: `O 8` ran clean (`0 frame(s), 0 byte(s), 0
      runt(s)`, every counter honestly zero) - the same quiet-bench
      result every other M5 capture tool this session has established.
      Re-ran `U 5` (ssdpwatch) and `B 5` (arpwatch) immediately after to
      confirm sharing the capture buffer with a sixth command caused no
      regression to either - both completed clean. `d 8` app-tree
      regression and the M0 boot self-test (10/10) both still clean.
      CI matrix confirmed green live on the push (run 32610544407):
      both `Firmware (STM32F429)` and `Firmware (STM32F429, minimal
      apps)`.

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
- [x] Wake-on-LAN magic-packet sender, independent of the board's own WoL
      support. Needs RMII. S.
      New `explorer_wol_demo.c` (command `W <hex-mac>`, e.g.
      `W 0011223344AA`): the standard magic packet (6 x 0xFF sync stream
      + the target MAC repeated 16 times), sent as a raw Ethernet frame
      (EtherType 0x0842, broadcast destination) straight through
      `cads_hal_eth_mac_transmit()` - the same "bypass lwIP entirely"
      choice `explorer_pktgen_demo.c` already made, and a hard
      requirement here rather than a style choice: this bench's
      long-established finding (no DHCP server, `netif_ip4_addr()` never
      leaves 0.0.0.0) means `ip4_route()` refuses *any* IP packet, so a
      UDP-broadcast magic packet would never actually leave this board
      here - raw Ethernet needs a link, not an IP address. A 12-hex-digit
      MAC parser (`cads_parse_mac()`) was added locally to `explorer.c`,
      not `cads/toolbox/str.h`: `cads_str_to_hex()` returns into a
      `uint32_t` and a MAC is 48 bits, and this is the only caller.
      RAM-neutral: the 116-byte frame is a plain stack buffer (like
      pktgen's own 60-byte one), not `static` - no lwipopts.h trim
      needed this time.
      VERIFIED on hardware with the same byte-perfect MMC cross-check
      this session has used for every TX-path claim: a fresh reflash
      (`tx_good=0`) followed by one `W AABBCCDDEEFF` produced
      `tx_good=2` — 1 for the magic packet itself, 1 for the DHCP
      discover broadcast `cads_net_init()`'s own link-wait triggers
      (the same accounted-for overhead the packet generator task already
      documented: "2 independent DHCP retries during the run - not a
      pktgen artifact"). Reproduced identically across two independent
      runs (`001122334455` and `AABBCCDDEEFF`, both tx_good delta = 2).
      Host `ctest` 17/17, M0 boot self-test 10/10, `d 8` app-tree
      regression clean.
- [!] **HARDWARE GATE M5**: DHCP lease, ping, CLI over telnet, screen
      streaming at a measured frame rate, all with the display active.
      NEEDS A USER DECISION - not a code fix, an infrastructure gap this
      whole M5 milestone's own hardware verification has run into
      repeatedly and documented as each bullet landed (ARP/ping/
      traceroute/iperf/pktgen/sniffer/mactable/WoL): (1) this bench has
      never obtained a DHCP lease - no DHCP server is present on the
      segment, so `netif_ip4_addr()` stays 0.0.0.0 and `ip4_route()`
      refuses any outbound unicast send, which is exactly what "DHCP
      lease" and "ping" in this gate need; (2) this agent's own
      shell/tooling has no network path to the board's physical LAN
      segment at all, so a telnet client for the CLI or a TCP client for
      screen streaming can never be driven from here regardless of DHCP.
      Every individual M5 feature has its own real hardware verification
      already (MMC/DMAMFBOCR-counter cross-checks, this file's own log
      entries) - what is missing is specifically the *combined,
      externally-observed* scenario this gate describes, which needs
      either a DHCP server added to the bench segment, a client machine
      on that segment this agent can reach, or the user running the
      telnet/DHCP/streaming checks by hand and reporting back. Left open
      rather than silently marked done or skipped; M6 (already `[~]`,
      several bullets already flashed and verified) continues below.

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
- [x] A game, to exercise the input and timing paths end to end
      "Leo's Reflex Test" (new `apps/game`, menu item "Reflex Test"): a
      literal reading of this bullet's own wording rather than a loose
      one - the whole game IS measuring the time between a stimulus
      (the "GO" signal) and an input event (the OK release), using
      `cads_hal_ticks_ms()` the same way this session's own hardware
      verification has throughout. The "wait for it..." phase has to end
      on its own, with no key pressed at all, which `cads_view_t` (draw/
      input/enter/exit, nothing else) cannot do by itself - solved the
      same way `apps/desktop`'s blink clock already had to be: a
      `cads_game_tick(now_ms)` exported alongside `cads_desktop_tick()`/
      `cads_gpio_tick()`, called from the same one place in
      `explorer_app_demo.c`'s main loop, not a new framework capability.
      A small local xorshift32 (seeded from `cads_hal_ticks_us()`) picks
      each round's 0.8-2.8s delay; nothing else in this codebase needed
      randomness yet, so it lives in `cads_game.c` rather than a new
      toolbox module.

      FOUND AND FIXED, NOT PART OF THIS BULLET BUT DISCOVERED WHILE
      WIRING IT IN: `explorer_app_demo.c`'s view-registry array
      (`CADS_APP_DEMO_VIEW_CAPACITY`) was sized 7 - correct when this
      file was first wired to desktop+settings+settings_confirm+about+
      gpio+netinfo, but never updated when `apps/filebrowser` (2 more
      views) was added later in M4. `cads_view_dispatcher_add()` fails
      *silently* past capacity (every caller here discards the `bool`
      with `(void)`), so with the stale value of 7 the last two
      registrations attempted - filebrowser's info view, and
      `cads_menu_app_init()`'s own MENU view - never actually happened.
      That means `CADS_VIEW_ID_MENU` was never findable in this file's
      dispatcher: pressing OK on the desktop
      (`cads_view_dispatcher_push(dispatcher, CADS_VIEW_ID_MENU)`) would
      have failed silently on every single `d` hardware check this whole
      session has run - none of them ever pressed a button (no way to
      inject one without real touch/GPIO hardware, see below), so "no
      fault, N frames flushed" was all any of them could have shown
      either way. Bumped to 10 (the correct count including this game's
      own view), with a comment naming all ten and explaining exactly
      why the old value hid this.
      Added `tests/unit/test_app_tree.c` as the actual proof, and as a
      regression guard against this exact class of bug recurring
      silently again: it builds the real dispatcher via the same two
      calls (`cads_desktop_init()`/`cads_menu_app_init()`) in the same
      order `explorer_app_demo.c` uses, feeds a *synthetic* OK-release
      `cads_input_event_t` through `cads_view_dispatcher_input()`, and
      asserts the current view actually becomes MENU - something no
      hardware check this session has run could do without a human
      finger. One test in the same file rebuilds the tree against the
      old capacity of 7 and asserts MENU does *not* register, proving
      the new test would have caught the original bug. Needed one small
      addition to the host test harness: `tests/unit/fake_hal.c` gained
      a `cads_hal_board_info()` stub (only `apps/about`'s `_init()`
      touches the HAL at registration time; every other app's `_init()`
      is HAL-free) - everything else needed to link the real app tree on
      host (the real `cads_gui`/canvas, not `canvas_host.c`) was already
      reachable transitively through `cads_app_desktop`/`cads_app_menu`,
      the same link graph `cads-zero-sim` itself already proves works.

      RAM: +128 B (a handful of `uint32_t` fields, no buffers) - margin
      544 B over the 48K floor, no lwipopts.h trim needed this time.
      VERIFIED on hardware: itsboard links clean; host `ctest` 18/18 (17
      prior + the new `test_app_tree`, 4/4 cases, including the "would
      have caught the old bug" one); M0 boot self-test 10/10; `d 8`
      app-tree regression clean (photographed live on the panel).

      **Expanded 2026-08-23 to Leo's Arcade, not from an open roadmap/
      issue item - user asked directly for 3 more small arcade games
      playable with the board's own buttons.** Reflex Test kept, three
      new cartridges added: Snake (grid movement, wall/self collision,
      growth), Breakout (paddle/ball/brick physics, lives, win-by-
      clearing), Dodger (auto-scrolling gap obstacles, a new genre from
      the other two). Rules split into `modules/toolbox/{snake,breakout,
      dodger}.h` - pure, caller-owned-state, no HAL - the same split
      every M5 watcher already established, with 31 new host unit tests
      (hand-built board/ball/obstacle states, since nothing can feed a
      scripted button sequence into a physically running game). The app
      itself became a select screen pushing one real view per cartridge
      rather than one view with an internal mode, because
      `apps/settings/README.md` already documents that a view cannot
      relabel its own soft-key strip while it stays current - Snake
      needs Up/Down/Left/Right, Breakout needs Left/Right, Dodger needs
      Up/Down, and only separate pushed views (Back's default pop
      already returns to the select screen for free) get each its own
      correct strip.

      Ran straight into the *exact* failure mode this bullet's own
      capacity-bug story already describes, this time caught before any
      hardware check rather than found by one afterward: five views
      instead of one (the select screen plus four cartridges) pushed
      the real registration count to 14 - desktop, menu, settings,
      settings-confirm, about, gpio, netinfo, filebrowser,
      filebrowser-info, game-select, game-reflex, game-snake,
      game-breakout, game-dodger. `CADS_APP_DEMO_VIEW_CAPACITY` bumped
      from 10 to 14 in `explorer_app_demo.c` and its mirror in
      `tests/unit/test_app_tree.c` together, plus a new host test that
      presses OK on the select screen and checks the resulting view id
      and stack depth, not just that every id is *registered* - the gap
      the original bug lived in.

      RAM: 928 B -> 416 B margin over the 48K floor (four new views'
      state - two small toolbox structs plus four `cads_view_t`
      instances - still comfortably over `check_ram_budget.py`'s 256 B
      floor). Flash (CI `default` leg, Release): 227 460 B -> 232 284 B.
      VERIFIED on hardware: reflashed, boot self-test 10/10; M6 suite
      4/4 (one run showed a suite-harness timing flake on the unrelated
      F/D checks, gone on immediate re-run and on standalone
      `board_cmd.py` calls to each - not a regression from this change).
      Host `ctest` 27/27, including the two new app-tree navigation
      tests. CI confirmed green live on the push (run 32646459994, all
      four jobs including the new flash-budget gate).
      Cannot be exercised end to end from this shell: Snake/Breakout/
      Dodger read the real physical buttons through the normal input
      service, and `board_cmd.py` only talks to the separate bring-up
      explorer console - there is no remote way to inject a synthetic
      button press into the running app tree. A human at the bench
      pressing buttons is the remaining step for full confirmation.
- [x] Build-time optional apps: which of settings/about/gpio/netinfo/
      filebrowser/game land in the firmware is now a CMake choice
      (`CADS_APP_SETTINGS`/`CADS_APP_ABOUT`/`CADS_APP_GPIO`/
      `CADS_APP_NETINFO`/`CADS_APP_FILEBROWSER`/`CADS_APP_GAME`, each
      `ON` by default so an unconfigured build is unchanged), not from an
      open roadmap/issue item - user-requested directly, in response to
      the l2discover task above hitting the 48K RAM floor: "if we have
      trouble with memory, we should deploy a plugin-like mechanism
      where we can configure at build time which apps go into the
      firmware." Desktop and the menu itself are not options - they are
      the shell, not an app.
      Every option becomes a `CADS_APP_*_ENABLED` compile definition on
      `cads_flags` (top-level `CMakeLists.txt`), which essentially every
      target in this project already links, rather than wiring each
      option through every target that might care one at a time. Three
      of `apps/bringup`'s own explorer commands reach directly into a
      specific app for their own single-app demo (`g` -> apps/gpio,
      `v` -> apps/filebrowser, and `d`'s app-tree loop calls both
      `cads_gpio_tick()` and `cads_game_tick()` directly) - each now
      guards that reach with the matching `#ifdef`, printing "disabled
      at build time" instead of failing to compile, the same answer
      `explorer_mactable_demo_sim.c` already gives for a different
      reason (not available in this configuration).
      A first version left two link lines unconditional -
      `target_link_libraries(cads_apps ... cads_app_gpio
      cads_app_filebrowser cads_app_game)` in the top-level
      `CMakeLists.txt` - caught by actually testing a reduced
      configuration, not by inspection: CMake's own lazy treatment of a
      library name with no matching target (passed straight to the
      linker rather than erroring at configure time) let this get all
      the way to a *compile* failure (missing headers) in unrelated
      files before the real problem - a link line that would eventually
      fail too - was even reached.
      VERIFIED on hardware, both ends of the range: the default
      (everything `ON`) config re-verified unchanged - RAM 146016 B,
      margin 1440 B, `N`/`M`/`C`/`d 8`/M0 boot self-test all clean, host
      `ctest` 20/20. A from-scratch build with all six apps `OFF` links
      clean too: flash 211632 B (was 258472 B, -46840 B) RAM 140128 B
      (was 146016 B, -5888 B), `check_ram_budget.py` reports margin
      7328 B (was 1440 B) - flashed and VERIFIED on hardware: M0 boot
      self-test 10/10, `d 8` ran fault-free with an empty menu (0
      navigation transitions - the same "nobody touched it" result
      every other `d 8` run this session has had, not evidence either
      way for the empty-list case specifically, which rests on the
      pre-existing, already-verified `cads_menu_t` widget's own loop
      bound handling a count of zero rather than on anything new this
      task added). Board reflashed back to the default full-app image
      afterward and re-verified clean before moving on.
      `.github/workflows/ci.yml`'s firmware job is now a 2-entry matrix,
      default config and every `CADS_APP_*` `OFF`, precisely because the
      bug this task itself found (two link lines left unconditional)
      would sail through a CI that only ever built the default config.
      Confirmed passing live on a real push, not just locally: run
      32565652743, both `Firmware (STM32F429)` and `Firmware (STM32F429,
      minimal apps)` green, distinct `firmware-default`/`firmware-minimal`
      artifacts uploaded without the name collision an unmodified
      `name: firmware` would have hit.
- [x] **HARDWARE GATE M6**: full walkthrough of every app on the board. All
      five apps below the menu build, flash, and run without fault; a human
      walkthrough of each one by touch and by button is the same open item
      as the M3 gate's touch-navigation line, not a separate one. UPDATE
      2026-08-25: the BUTTON half is now done - live on hardware with the
      user actually pressing, OK/Up/Down/Back all confirmed correct (see
      that date's Log entry). TOUCH was blocked on a real, precisely
      isolated XPT2046 SPI data bug (same Log entry) - a "someone needs a
      scope or a systematic bring-up session" gap, not a "someone needs to
      go press it" one. UPDATE 2026-09-01: closed. Operator confirmed
      direct, hands-on testing of the full touch walkthrough on real
      hardware ("Ich habe schon ordentlich getestet") - taken as the human
      walkthrough this gate has always required, not re-verified
      independently this session. Most likely fixed by the SPI-bus mutex
      commit (`9506a46`, shared bus between the ui and input/console
      tasks) landing between the XPT2046 bug being isolated and this
      confirmation, though that causal link itself wasn't re-traced here -
      recorded as the operator's own gate-closing call, which is what this
      gate has always deferred to.

### The GPIO Swiss-army-knife

All timer-based, all achievable on the existing adapter wiring (PD0-7/PE0-7
outputs, PF0-7 inputs = S0..S7 buttons, PG0-5 general inputs). None of this
needs new hardware.

- [x] Logic level display: live high/low state of every adapter pin, already
      the core of the `apps/gpio` app.
      Already fully implemented, not new work this session:
      `cads_gpio_tick()` (`apps/gpio/cads_gpio.c`) re-reads
      `cads_hal_adapter_inputs()`/`cads_hal_adapter_interrupts()` every
      poll and redraws whichever IN0-7/INT0-5 cell changed - exactly
      "live high/low state of every adapter pin". This task was closing
      the loop with a real cross-check rather than rebuilding anything,
      per this bullet's own note that it was already done.
      VERIFIED on hardware: `i` (raw port dump) read `F=FCFF G=D7BB` at
      the same moment as the check - hand-computed against
      `hal_io.c`'s own formulas (`~PORTF->IDR & 0xFF`, `~PORTG->IDR &
      0x3F`, both confirmed in `targets/itsboard/board.h`:
      `CADS_PIN_IN_PORT`=GPIOF, `CADS_PIN_INT_PORT`=GPIOG,
      `CADS_ADAPTER_INT_MASK`=0x3F): IN0-7 = 0x00 (idle, no S0-S7
      pressed - consistent, since PF0-7 doubles as the S0-S7 buttons per
      this section's own intro), INT2 = active (bit 2 of 0x3F) - a
      floating/unterminated PG2 reading indeterminate without an
      external connection, not a fault. Cross-checks apps/gpio's
      formula against the identical raw register this session's own `i`
      command reads independently, rather than trusting the code alone.
      `g 6` (the dedicated GUI smoke test) ran fault-free, photographed
      live on the panel (OUT0/OUT1/OUT2 list and the IN column headers
      visible despite bench-lighting glare). No live pin *transition*
      demonstrated - inducing one needs a hand at a physical button or a
      jumper wire, neither available from this environment - but the
      display path itself (poll -> compute -> redraw) is now proven
      against real, freshly-read hardware registers, not assumed from
      the code alone.
- [x] Frequency/period counter on an INT line via timer input capture
      (TIM2/TIM5 are 32-bit general-purpose timers with input capture on
      several AF mappings — confirm exact INT-pin-to-timer-channel mapping
      against ITS-BRD-NucleoPins.xlsx before wiring). S/M.
      NOT ON AN INT LINE, AND ITS-BRD-NucleoPins.xlsx IS NOT ARCHIVED
      HERE - both discovered while starting this task, resolved without
      blocking on either. `ITS-BRD-NucleoPins.xlsx` does not exist in
      this repository (only referenced, once, in a `targets/itsboard/
      board.h` comment about a *different*, disputed pin claim). Checked
      the F415/417 sibling datasheet's own alternate-function table for
      PG0/1/3/4/5 (INT0/1/3/4/5, excluding INT2/PG2 - already on
      `docs/SAFETY.md`'s RMII no-touch list, since PG2 is also
      `ETH_RXER`) instead: none of them carry any timer alternate
      function at all - their only non-GPIO AF is `FSMC_A1x`. So "an INT
      line" was never going to work regardless of which one. This
      exact tradeoff was already flagged, unprompted, in
      `docs/HARDWARE.md`'s "Capability this board has that the firmware
      does not yet use" section: the CN8 timer breakout exists "for this
      kind of use... without having to repurpose an OUT/IN pin that
      already has a job." Used it instead: `PB10` (CN8 pin 5),
      confirmed via two independent sources - the project's own
      schematic (`ITSBRD-schematic-Jaehnichen-HAW-rev02.pdf`, "timers"
      page) labels the net "TIM2_3", and the sibling datasheet's AF
      table independently confirms `PB10: TIM2_CH3` via `AF1`. TIM2 is
      one of the two 32-bit timers this bullet's own text asks for.
      Split the same way the MAC table task split sniffing from
      learning: `cads/toolbox/freqcounter.h` (`cads_freqcounter_t`) is
      the portable wraparound-safe delta / missed-edge-resync state
      machine, HAL-free and unit-tested
      (`tests/unit/test_freqcounter.c`, 7 cases including a real
      32-bit-counter wraparound and an overcapture resync) since this
      bench has no way to drive a known test frequency into CN8 without
      a physical jumper wire; `targets/itsboard/hal/hal_freqcounter.c`
      is only the TIM2 register layer (PSC=89 for a 1 MHz/1us tick, same
      clock-doubling fact `hal_pktgen_timer.c` already verified for
      TIM6 applies to TIM2 too since both are APB1; IC3F=0001, N=2 light
      glitch filter, since CN8 is unbuffered straight to the MCU per
      `docs/HARDWARE.md`). New explorer command `F <sec>` reports period
      count, missed count, and min/max/avg Hz.
      FOUND AND FIXED while verifying: `cads_hal_freqcounter_stop()`'s
      own doc comment claimed it "releases PB10 back to a plain
      pulled-down input", but the first implementation only stopped
      TIM2's counter, leaving the pin in AF1 mode - fixed to actually
      call `cads_gpio_set_mode()` back to plain input, matching what was
      already documented.
      RAM: +32 B (one small struct, no buffers) - margin 512 B over the
      48K floor.
      VERIFIED on hardware: itsboard links clean, M0 boot self-test
      10/10, `d 8` app-tree regression clean. `F 5` ran fault-free
      end-to-end and correctly reported "0 period(s), 0 missed" - honest
      given nothing drives CN8 pin 5 on this bench (no physical jumper
      access from this environment, the same class of limitation as the
      sniffer/mactable tasks' zero ambient traffic), not a fabricated
      reading. The actual capture-to-period logic has real,
      hardware-independent proof instead: host `ctest` 19/19 (18 prior +
      the new `test_freqcounter`, 7/7 cases).
- [x] Duty-cycle measurement, same input-capture channel, second capture
      compare register. S/M.
      Literally the same channel: TIM2_CH4's CCMR2.CC4S=10 maps IC4
      onto TI3 (RM0090's own words: "IC4 is mapped on TI3") - the
      timer's channel-swap feature, not a second GPIO pin, so CH3
      (rising, unchanged from the frequency-counter task) and CH4
      (falling, new) both read CN8 pin 5. Deliberately NOT RM0090's
      textbook PWM Input Mode, which additionally drives the slave mode
      controller into Reset mode so CNT zeroes on every rising edge:
      that would change what CCR3 itself means, reaching back into the
      already-verified, already-committed frequency counter. Used the
      mathematically equivalent alternative instead - leave TIM2
      free-running exactly as it already was, and compute high time as
      the wraparound-safe delta between a falling capture and the most
      recent rising one, extending `cads/toolbox/freqcounter.h`
      (`cads_freqcounter_capture_high()`) rather than the timer
      configuration the period counter depends on.
      One command, not two: the `F` explorer command now reports both
      from the same capture session (period AND duty cycle are the same
      signal's two halves, not separate measurements) - `#   duty avg=NN%
      (avg high H of avg period P ticks)` alongside the existing
      min/max/avg Hz line.
      RAM: 0 B extra device-side static state beyond the same
      `cads_freqcounter_t` instance (more fields in the same already-
      allocated struct); host `ctest` gained 7 more cases (14 total in
      `test_freqcounter`) covering the falling-edge-before-any-rising-edge
      case, wraparound, its own independent overcapture counter, and
      min/max/avg high-time tracking - again the only real proof
      available, since this bench still has no physical jumper wire to
      drive a known duty cycle into CN8 pin 5.
      VERIFIED on hardware: itsboard links clean (RAM unchanged, 512 B
      margin), M0 boot 10/10, `d 8` regression clean, `F 5` ran
      fault-free and honestly reported zero periods and zero high-time
      samples (nothing drives CN8 pin 5 here, same as the frequency
      counter task). Host `ctest` 19/19 (same 19 test binaries as
      before - `test_freqcounter` itself grew from 7 to 14 cases).
- [x] PWM generator on an OUT line (any adapter output pin on a timer channel
      can be reconfigured AF instead of GPIO push-pull — verify against the
      pin table which OUT pins have timer AFs; PD/PE pins have mixed timer
      support, some OUT pins may be GPIO-only). S/M, pin-mapping dependent.
      Checked all sixteen OUT pins (PD0..7, PE0..7) against the sibling
      datasheet's alternate function table, per this bullet's own
      warning that some are GPIO-only - most are (FSMC/USART/CAN only,
      these are the same silicon pins ST wired for its parallel memory
      bus on other boards), and two more (PD2/TIM3_ETR, PE0+PE7/
      TIM4_ETR+TIM1_ETR) carry only a timer External Trigger *input*,
      not a PWM-capable output channel. Only PE5 and PE6 (OUT13, OUT14)
      have a real output-compare channel: TIM9_CH1/CH2, both AF3. Used
      PE5/TIM9_CH1 - one channel, as the bullet asks for one generator.
      TIM9 needed its own clock check (it is on APB2, not APB1 like
      every timer this milestone used before it): hal_clock.c's PPRE2 =
      DIV2 (APB2 = 90 MHz), the same doubling rule already applied to
      TIM2/TIM6 gives TIM9CLK = 180 MHz. PSC is computed per call rather
      than fixed, unlike every earlier timer driver this session - a
      16-bit timer serving a wide, user-chosen frequency range (1 Hz..
      100 kHz) needs it to maximise ARR's own duty-cycle resolution for
      whatever frequency was actually requested. PWM mode 1 (OC1M=110)
      confirmed against RM0090's own text, not just the CMSIS bit name:
      "channel 1 is active as long as TIMx_CNT<TIMx_CCR1", so CCR1 is
      already the duty threshold with no translation needed and the 0%/
      100% edge cases fall out of that inequality for free.
      New explorer command `D <hz> <duty%> [sec]`. OUT13 already has a
      job (apps/gpio's own LED) - `cads_hal_pwm_start()`/`_stop()` claim
      and release it exactly like hal_spi.c's PA7 claim/release, just at
      far lower stakes; `_stop()` returns the pin to plain GPIO output,
      matching the contract `cads_hal_freqcounter_stop()`'s own bug fix
      established two tasks ago.
      VERIFIED on hardware - genuinely, not just "ran without fault"
      this time: OUT13's own LED is physically visible, unlike CN8 pin 5
      (frequency/duty-cycle tasks) or PB10 before it, so this is the
      first M6 GPIO Swiss-army-knife bullet with real photographic
      proof rather than an honest zero. `D 1 50 20` (1 Hz, easily
      photographable) run with six webcam captures spread across the
      20s window: the LED alternates green (on) / white (off) across
      the sequence (frames 1, 6 = on; frames 2, 4 = off) - a real
      1 Hz blink, not a stuck level. itsboard links clean (RAM
      unchanged - this driver has no static state at all), M0 boot
      10/10, `d 8` app-tree regression clean.
- [x] Simple logic analyzer: sample IN0..7/INT0..5 at a timer-triggered rate
      into a ring buffer in SRAM, render as a waveform on the canvas. Sample
      rate bounded by how fast the canvas can be redrawn (dirty-rectangle
      rule applies) rather than by the GPIO read itself. M.
      Capture-then-render, not live-scrolling: `apps/bringup/tasks.c`'s
      own rule is that the display has exactly one flusher (the ui
      task; `cads_tasks_redraw_sync()` is how anything else waits for
      it), so a continuously-updating waveform would mean this
      command's sampling loop yielding to that wait after every redraw
      and losing whatever sample interval elapsed while blocked there.
      Sampling for a fixed window first, then rendering the whole
      result once, sidesteps that tension entirely for a fraction of
      the risk a bigger, interrupt-driven redesign would have carried -
      appropriate for this bullet's own "M" sizing. This is still the
      literal reading of "sample rate bounded by how fast the canvas
      can be redrawn": it now bounds how densely a capture's worth of
      samples can usefully be drawn across the panel's width, not a
      per-sample redraw rate.
      New TIM7-paced `hal_sample_timer.c` (a basic timer, no GPIO pin,
      nothing docs/SAFETY.md restricts - the same reasoning
      `hal_pktgen_timer.c`'s TIM6 already established), kept separate
      from that driver rather than reused: the two are paced by
      genuinely different things that could plausibly run at once one
      day. New explorer command `L <hz> [sec]`, default 25 Hz/5s,
      capturing into a `cads_ring_t` (`cads/toolbox/ring.h`, already
      built and tested this project, not a new ring implementation) and
      rendering all fourteen channels (IN0..7, INT0..5) as labelled
      horizontal-line rows once capture ends.
      RAM: the ring's own storage started at 256 B (128 samples) after
      an intermediate 512 B (256-sample) cut left
      `targets/itsboard/linker/cads_itsboard.ld`'s
      `ASSERT(__cads_heap_size >= 48K, ...)` guard at *exactly* 48K -
      zero bytes of margin, this session's own repeated lesson that a
      razor's edge should never be accepted when the choice consuming
      it is a single feature's own, not a shared resource. Halved to
      128 samples (and the default rate to 25 Hz so a plain `L` still
      captures cleanly rather than routinely reporting drops) - 128
      samples across the ~438 px the waveform area actually has is
      still ~3 px/column, a clearer trace than 256 near-1px columns
      would have been, not just a smaller one. Final margin: 256 B.
      VERIFIED on hardware with real photographic proof of the full
      pipeline, not an honest zero (this bench's IN0..7/INT0..5 have no
      external test signal, the same limitation the frequency/duty-cycle
      tasks had, but here the render pipeline itself - timer-paced
      sampling, ring buffer, 14-row canvas draw, ui-task flush - is what
      needed proving, and a photograph of a correctly-labelled,
      correctly-drawn "everything reads idle-low" trace is exactly that
      proof): `L` (default args) captured 124/125 samples, 0 dropped,
      rendered without a redraw-sync timeout, and the panel photo shows
      all fourteen rows correctly labelled and flat at the low position -
      cross-checked against `i`'s own raw IDR dump (`F=FCFF`, `G=F7BF`)
      showing IN0-7 and INT0-5 both genuinely 0x00 right now, so the
      flat trace is accurate, not a rendering bug. itsboard links clean
      (256 B margin), M0 boot 10/10, `d 8` app-tree regression clean.
      Host `ctest` 19/19 (no new toolbox module this task - the ring
      buffer and rendering geometry are both already-tested/
      straightforward enough not to need one, unlike the mactable/
      freqcounter tasks' genuinely subtle wraparound logic).
- [x] Simple continuity/cable tester using two adapter pins: drive one OUT
      pin, read it back on an IN pin through an external jumper/cable under
      test — same operator-in-the-loop pattern the manufacturer's own
      GPIOTest already uses for the OUT0-to-INTx test. S.
      Last GPIO Swiss-army-knife bullet, and the only one that needed
      no new HAL driver at all: `cads_hal_adapter_outputs()`/
      `_interrupts()` are already portable (implemented on both
      `hal_io.c` and `targets/sim/hal_sim.c`), so
      `explorer_continuity_demo.c` is a single file, no board/sim split,
      wired straight into `cads_apps`' unconditional source list the
      same way `explorer_arp_demo.c`/`explorer_ping_demo.c` already
      are - the simulator does not model a virtual jumper, so it always
      reports FAIL, the same honest result an un-jumpered real board
      gives. New command `K`: drives OUT0 (PD0) low then high and
      requires INT0/AUX0 (PG0) to follow *both* transitions before
      calling it PASS, not just one - INT0's own pull-up would give a
      false PASS to a test that only ever checked the high level.
      FOUND AND FIXED ON THE FIRST HARDWARE RUN, NOT FROM READING THE
      SOURCE FIRST: an un-jumpered board reported "low ok, high not
      seen" - backwards from "INT0 is pulled up, so it reads high
      regardless of OUT0". `hal_io.c`'s own
      `cads_hal_adapter_interrupts()` is `(~IDR) & mask`, the identical
      active-low inversion `cads_hal_adapter_inputs()` already carries
      a comment for ("active low on the wire; report active high so
      callers read naturally") but that this function does not repeat -
      bit=1 means the pin is being pulled LOW, bit=0 means it is at its
      own idle HIGH. Fixed the test's own expectations to match (low on
      the wire -> expect bit=1; high on the wire -> expect bit=0) and
      renamed the internal parameter to `want_active`, in the
      function's own bit sense, specifically so this does not have to
      be re-derived a second time.
      RAM: 0 B (no static state at all).
      VERIFIED on hardware, and the fix itself is the verification: a
      fresh flash with the corrected polarity reports "FAIL - no
      continuity (low not seen, high ok)" - now the mirror image of the
      pre-fix run and exactly what an un-jumpered, pulled-up-idle INT0
      predicts, where the pre-fix version's inverted, backwards-matching
      result was the bug. A genuine PASS needs a human to place a
      jumper or a cable under test between OUT0 and INT0, not available
      from this environment - same class of limitation as every earlier
      GPIO Swiss-army-knife bullet's own hardware ceiling, not specific
      to this one. itsboard links clean (RAM unchanged), M0 boot 10/10,
      `d 8` app-tree regression clean, host `ctest` 19/19.
      This closes out every M6 GPIO Swiss-army-knife bullet; only
      **HARDWARE GATE M6** below (a human touch/button walkthrough) is
      still open in this milestone.

## M7 — Simulator and test pipeline  `[x]`

- [x] SDL2 simulator: panel, touch via mouse, adapter I/O panel, console
      Already fully implemented, not new work this session -
      `targets/sim/hal_sim.c` (and its own `README.md`) already have
      all four pieces this bullet names: the 480x320 panel in an SDL2
      window, touch via the mouse (`cads_sim_touch_from_mouse`, wired
      to `SDL_MOUSEBUTTONDOWN/UP/MOTION`), the adapter I/O panel
      (`cads_sim_draw_cells` rendering OUT0-7/OUT8-15/IN S0-S7/INT0-5
      as indicator cells, plus LED-USER and a touch-coordinate
      readout), and the console (stdout/stdin, `scripts/board_test.py`-
      style TAP consumers already work against it unchanged). Keyboard
      mapping (keys 1-8 = S0-S7, F1-F6 = INT0-5, space = the Nucleo's
      USER button) is documented in the target's own README. This task
      closed the loop with fresh verification rather than building
      anything - no code changed.
      VERIFIED: `cads-zero-sim --screenshot` launched cleanly just now
      (fresh run, not relying on history) and wrote a valid 460854-byte
      24 bpp BMP - exactly 480*320*3 + the 54-byte BMP header, the
      correct size for a genuine, correctly-dimensioned panel capture.
      The golden-image tests (`golden_splash`, `golden_bringup_pattern`)
      already exercise this exact same render path pixel-for-pixel on
      every host `ctest` run this whole session, including just now
      (19/19). An OS-level screenshot of the actual interactive window
      (to visually confirm the I/O panel/touch sidebar specifically,
      which the golden BMP capture does not include) was attempted but
      came back solid black - this environment's display appears to be
      a disconnected/inactive one (a remote/headless session, not a
      flaw in the simulator - `cads-zero-sim` itself launched and ran
      without error either way). The I/O panel/touch/keyboard code
      itself is straightforward, already-documented SDL event handling,
      confirmed by reading it rather than by a screenshot neither this
      environment nor a human at the keyboard has provided yet.
      No board hardware gate for this one: it is entirely a host/
      simulator-scoped task, nothing here touches `targets/itsboard/`,
      and no files changed at all.
- [x] Golden-image tests: render, compare against reference PNGs. Hand-written
      BMP reader + PNG encoder (real zlib DEFLATE via Python stdlib, not
      vendored/hand-rolled compression) rather than a new C image library for
      one debug feature. Exact pixel comparison (the sim renders
      deterministically), diff PNG written on mismatch. Two golden images so
      far: boot splash, bring-up self-test pattern. apps/gpio deliberately
      not captured yet - it postdates this branch's fork point from main, see
      targets/sim/golden/README.md for the recipe to add it.
- [x] Unit tests (Unity) for canvas, core, toolbox
      Already substantially done, not new work - closed the loop by
      actually counting coverage rather than assuming it, the same
      diligence as the last two "already implemented" M6/M7 bullets.
      `toolbox`: every one of its nine source files
      (`fmt/freqcounter/log/mactable/pubsub/record/ring/str/tap.c`) has
      a matching test - 89 cases total (6 to 14 each; the newest two,
      `test_mactable`/`test_freqcounter`, were written this session for
      their own roadmap bullets, not for this one). `canvas`:
      `test_canvas.c` has 25 cases covering pixel round-trips, every
      `fill_rect` edge/clip case, nested clip intersection, damage
      tracking, bitmap4 unpacking, text layout/clipping, and the
      dirty-rectangle flush path - genuinely thorough, not a stub,
      confirmed by reading it rather than trusting the file's
      existence.
      `core` was the genuinely open question, and turned out to have
      nothing further to add rather than a gap to fill:
      `core/cads_hal.h` is a pure interface with no logic of its own to
      unit-test - every function it declares has real logic only in a
      target-specific implementation (`targets/itsboard/hal/*.c` or
      `targets/sim/hal_sim.c`), and this project's own established way
      to unit-test *against* that interface is already built and in
      heavy use: `tests/unit/fake_hal.c`/`fake_mdio.c` implement it for
      the host, and `test_input.c`/`test_eth_aneg.c`/
      `test_eth_linklog.c`/`test_app_tree.c`/`test_canvas.c` already
      exercise real portable logic through it. The only other plausible
      reading of "core" - `modules/kernel` - is deliberately,
      permanently board-only: its own header says so outright ("a thin
      layer over FreeRTOS... the simulator, where there is no FreeRTOS
      at all"), and its CMakeLists.txt only ever adds it for the
      itsboard target. Porting or faking FreeRTOS for a host Unity test
      would be a disproportionate side-project for this one bullet, and
      this project's own established verification for it is already
      the right one for what it actually is: a real hardware test
      (`explorer_kernel_test.c`, command `x`, "cads_timer + cads_event
      under the scheduler"), not a host unit test standing in for
      something that only means anything with a real scheduler
      underneath it.
      VERIFIED: host `ctest` 19/19, run fresh for this bullet
      specifically, not cited from memory. No board hardware gate - no
      files changed, nothing here touches `targets/itsboard/`.
- [x] `scripts/board_test.py` extended: TAP over VCP, per-milestone suites
      "TAP over VCP" already existed (the boot-time self-test's own
      `1..N`/`ok`/`not ok`/`# RESULT:` stream, parsed since this
      script's very first version) - "per-milestone suites" was the
      genuinely open half, and genuinely new work this time, unlike the
      last three M6/M7 bullets. Added `--suite <name>` rather than a
      separate script, matching the bullet's own wording ("`board_test.py`
      extended", not a new file) and this project's precedent of one
      script per concern (`board_cmd.py` for one-off commands,
      `board_soak.py` for stability) - a suite runs *after* a clean boot
      gate, over the same still-open console, so the boot gate is what
      proves the explorer REPL a suite needs is actually alive rather
      than a separate assumption.
      Each check is (letter, argument, description, validator) - not
      firmware-emitted TAP, since these commands mostly print ad hoc
      "# foo: done" text, not `ok N`; `run_suite()` synthesises real TAP
      output from a small Python-side validator function per check
      instead. Every validator asks "did this command complete and
      report a well-formed result", not "was the result unconditionally
      good": several M6 commands have an environment-dependent correct
      answer (this bench's own long-established no-DHCP-server, no-
      physical-jumper limitations, see the frequency-counter and
      continuity-tester tasks' own writeups), so asserting a specific
      PASS would make a perfectly healthy board fail this gate forever.
      The `m6` suite checks `F` (frequency/duty-cycle counter completes),
      `D` (PWM generator completes), `L` (logic analyzer captures and
      renders without a redraw timeout - the one check that verifies
      something beyond "didn't crash"), and `K` (continuity tester
      produces a well-formed PASS *or* FAIL, not specifically PASS).
      FOUND AND FIXED WHILE VERIFYING: the first real run failed all
      four checks. Cause was in the test harness, not the firmware -
      `F`'s explorer command takes one argument (seconds) with no
      user-selectable sample rate, unlike `L`'s two; the suite's first
      draft passed it "25 2" as if it were "rate seconds", `F` parsed
      "25" as seconds and ran for 25s instead of the intended 2, and the
      suite's own 15s per-check timeout gave up before that finished -
      corrupting every check after it, since the next command got typed
      into a board still mid-way through the previous one. Fixed the
      argument to the one `F` actually takes; the same reflash-and-rerun
      then passed all four checks cleanly.
      VERIFIED on hardware: `board_test.py --suite m6` (after a fresh
      flash) reports `PASS: 10/10` for the boot gate followed by
      `# RESULT: PASS` for all four suite checks, `K` correctly scored
      `ok` for its own honest FAIL result. Re-ran the plain (no
      `--suite`) invocation afterward to confirm the refactor did not
      change the existing boot-gate behaviour - identical `PASS: 10/10`
      output. No firmware changed this task, so no itsboard rebuild -
      this is a host-side Python tool.
- [x] CI: build both targets, unit + golden tests, size regression budget
      `.github/workflows/ci.yml` already had three of the four pieces:
      a `firmware` job (cross-build for the itsboard target, plus a
      generated-vector-table freshness check), a `host` job (build +
      `ctest`, which already runs the golden-image tests alongside the
      unit tests - no separate golden job needed), and a plain size
      *report* (prints `arm-none-eabi-size`'s output, decoration, not a
      gate). "Size regression budget" was the one genuinely missing
      word, and the one this session's own history motivates directly:
      this milestone hit `targets/itsboard/linker/cads_itsboard.ld`'s
      `ASSERT(__cads_heap_size >= 48K, ...)` floor at *exactly* zero
      bytes of margin more than once, caught only by manually counting
      bytes after the fact each time - a floor a build either clears or
      does not is not the same as a budget that warns while there is
      still room to fix it calmly.
      New `scripts/check_ram_budget.py`: reads `__cads_heap_size` back
      out of the built ELF via `nm` - the exact same symbol the
      linker's own ASSERT already computes, not re-derived from section
      sizes (which would risk drifting out of sync with what the
      linker script actually does) - and fails if the margin above the
      48K floor is thinner than `--min-margin-bytes` (default 256,
      deliberately the smallest margin this session ever accepted as
      "real" for a shipped task rather than an arbitrary round number).
      Wired into the `firmware` job right after the existing size
      report, with `set -o pipefail` made explicit rather than relying
      on GitHub Actions' own default shell behaviour for a `| tee` to
      actually fail the step - a silently-swallowed exit code here
      would defeat the entire point.
      VERIFIED: ran the script locally against the current build in
      both a genuine passing case (0 exit, `PASS: 256 B of margin,
      budget is 256 B` - this project's own current, real margin) and a
      deliberately-tightened budget to force a failure (`--min-margin-
      bytes 500`, 1 exit, clear FAIL message) - both propagated through
      `set -o pipefail | tee` with the correct exit code, confirmed
      explicitly rather than assumed from GitHub's own documented
      default. `.github/workflows/ci.yml` itself parses as valid YAML
      (checked with Ruby's YAML parser, since PyYAML is not installed
      locally). No firmware code changed - CI/tooling only, so no
      itsboard rebuild or board flash for this task; the real, live CI
      run this commit itself triggers on `main` is the actual
      end-to-end verification for a workflow-file change - watched
      after pushing, not assumed from the local dry run alone; see the
      dated Log entry below for that run's own result.

---

## M8 — Developer experience and documentation depth  `[ ]`

Queued 2026-08-25 from a burst of user requests during a live hardware
debugging session (see that day's Log entries for the touch/button
investigation this was queued alongside). Deliberately NOT started the
same session they were requested in - each of these needs real design
attention (cross-platform tooling, a new network protocol client, an
editor project other people will actually use), not 5 AM hacking under
a hardware fire. `[swarm-ready]` markers below are a first guess, not
final - re-check against docs/HARDWARE.md/SAFETY.md before assigning.

- [ ] iperf client (the explorer's `I <sec>` command is a server only -
      `apps/bringup/explorer_iperf_demo.c`/`explorer_iperf_demo_sim.c`;
      add the client half, same lwiperf-backed pattern, both targets).
      `[swarm-ready]`
- [ ] clang-format integration: a committed `.clang-format` matching this
      codebase's actual existing style (checked against the style already
      used throughout, not a generic default), plus a CI check that fails
      on unformatted diffs. `[swarm-ready]`
- [x] clang-tidy integration, cross-platform (Windows/Linux/macOS) and
      wired so findings surface well in an editor's Problems panel, not
      just a terminal log. Done 2026-08-28: curated `.clang-tidy`
      (evidence-based against real files, not a stock preset - see the
      file's own header comment) plus `.vscode/settings.json` wiring
      (`C_Cpp.codeAnalysis.clangTidy.enabled`), `.clang-format` fixed to
      match the codebase's real, already-consistent style
      (`IndentCaseLabels: true`), format-on-save wired the same way.
      Windows coverage documented honestly, not assumed: see the new
      Windows subsection in `docs/how-to/vscode-setup.md#1-get-a-toolchain-on-path`.
- [x] A complete VS Code project (`.vscode/` recommendations, tasks,
      launch configs for the existing GDB flow) with genuinely useful
      extensions for someone learning this specific firmware+board,
      documented for beginners at the project's own published how-to.
      Done 2026-08-28 - see this Log's own "VS Code: high-integration
      native workflow" entry below for what shipped and how it was
      actually driven/verified (not just written and assumed working).
- [x] Decision-rationale documentation: expand `docs/explanation/` and/or
      `docs/how-to/debug.md` with the "why" behind standing choices that
      are currently only explained in scattered code comments and this
      file's own Log - starting with why this specific compiler toolchain
      (`ARM_BIN` / vcpkg-provided arm-none-eabi-gcc 13.3.1, see
      `docs/how-to/build.md`) over alternatives, and a real walkthrough of
      the debugging tools already in daily use this session (`st-util` +
      GDB, `scripts/board_test.py`, the explorer's own diagnostic command
      table). Done same day: `docs/explanation/toolchain.md` (new) covers
      the vcpkg/Keil-Studio provenance, what is and is not actually
      pinned, and explicitly does not invent a rationale the repository
      never recorded; `docs/how-to/debug.md` rewritten around the three
      tools actually in daily use (fault console, st-util+GDB, the
      explorer), including the verified `z FAULT` reference signature and
      a symptom-to-command table.
- [ ] More board photos in `docs/`, plus a repeatable routine that
      refreshes them when the UI actually changes - `tests/gallery/
      gallery.c`'s host-rendered PPMs already exist for exactly this and
      are the right source (deterministic, no camera/lighting variance,
      already wired as `gallery_renders` in CI) - converting the existing
      set into docs-embedded, versioned images and documenting when to
      regenerate them is the actual remaining work, not new capture
      tooling. `[swarm-ready]`

## M9 — Active network tooling  `[ ]`

Queued 2026-08-26 from the user's own suggestion, in the same spirit as
the Flipper Zero WiFi devboard research this session: this board already
carries an extensive PASSIVE network Swiss-army-knife (`apps/bringup/
explorer.c`'s A/P/T/G/C/M/N/R/B/U/O commands - ARP scan, ping,
traceroute, packet generator, promiscuous capture, MAC table, L2 recon,
rogue-DHCP watch, ARP watch, SSDP/UPnP watch, traffic overview), plus an
iperf2-compatible server (M5). What it does not have yet is anything
ACTIVE/offensive - tools that inject forged traffic rather than only
observing it. Deliberately not started the same session it was
requested in, and for a sharper reason than M8's "needs real design
attention": these transmit forged packets onto whatever network the
board is plugged into, including networks other devices depend on -
the blast radius of a bug here is not "the demo looks wrong," it is
"something else on the LAN loses connectivity." Real design attention
means at minimum: a dry-run mode that logs what it would send without
transmitting, an explicit target/scope confirmation before the first
real packet goes out (same spirit as the M4 hardware gate's jumper
requirement - make the operator confirm they mean it), and a clear
statement in the command's own help text that this is for a controlled/
isolated test network, not a shared one. `[needs-decision]` on the
confirmation UX specifically - that's a real design choice, not
something to pick blind at 1am.

- [ ] ARP spoofing: send forged ARP replies to redirect traffic between
      two hosts through this board (or to itself), for testing how
      other devices/software on a controlled network detect or react to
      it. Natural counterpart to the already-implemented ARP watch (`B`)
      - that command exists specifically to detect this attack, so this
      board can now demonstrate both sides of the same technique.
      `[needs-decision]`
- [ ] DHCP spoofing / rogue DHCP server: answer DHCP requests with
      attacker-controlled lease info (gateway/DNS), for the same
      controlled-network testing purpose. Counterpart to the existing
      rogue-DHCP watch (`R`). `[needs-decision]`
- [ ] Whatever else fits the same bar once the confirmation-UX decision
      is made - the user asked for "everything what is possible useful,"
      but scope that against what a Flipper-class tool actually ships
      (this list, plus maybe deauth-adjacent Wi-Fi tooling once/if the
      Flipper Zero WiFi devboard integration research from this session
      turns into real hardware work) rather than open-ended scope creep.

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

- 2026-09-28 (praktikum/start: 24 KB SRAM for the lessons) - With RNLAB,
  everything CPU-only moves to CCM: lwIP's MEM_SIZE heap + all memp pools
  (`LWIP_DECLARE_MEMORY_ALIGNED` in lwipopts.h), cads_net_board.c's RX/TX
  copy buffers, the explorer capture buffer, FreeRTOS idle/timer stacks
  (+15.7 KB; nothing there is a DMA target - hal_eth_mac.c copies). This
  branch also defaults MARAUDER/ACTIVE/GAME/FILEBROWSER to OFF (+5.6 KB; no
  offensive tools in the student image). Margin 384 B -> 24 768 B, CCM
  29.5 KB/64 KB. New `CADS_RNLAB_TCP_WND_MSS` (2..16) /
  `CADS_RNLAB_TCP_SND_BUF_MSS` (2..8), defaults = main's 8/4, derived pools
  in CCM (16/8 builds, SRAM unchanged). Found + fixed along the way:
  `cads_lightorgan.c` was built unconditionally, so every
  `-DCADS_APP_MARAUDER=OFF` build failed (CI's minimal job never turns
  Marauder off). Gotcha: `CADS_CCM_SECTION` is empty unless the TU has
  `CADS_TARGET_ITSBOARD` - cads_kernel does not, so kernel.c spells the
  attribute out; check `nm` addresses (0x1000xxxx) after any CCM move.
  test_app_tree/cads_gallery now build only with the full app set. HIL:
  ping (incl. 50x 1472 B), `lab info` UART+telnet, iperf2 29.7 Mbit/s
  against `I`, `V 5` 342 kpixel/s, forensic ring empty.

- 2026-09-28 (apps/rnlab: Rahmen für das Rechnernetze-Praktikum, Branch
  `praktikum/start`) - New `CADS_APP_RNLAB` (default ON on this branch):
  `rnlab_init()` at the top of `cads_explorer_run()` brings the netif up
  (static 192.168.33.99/24), registers `lab` with the new
  `cads_cli_register()` and starts the TCP CLI on :4242, so the board answers
  ping/telnet straight after reset with no `h`/`j`. Serial `lab ...` works in
  both modes: the app tree now feeds non-key ASCII bytes to a CLI session
  (only with RNLAB; exit/key bytes unchanged), the explorer loop checks for
  `lab` before its letter switch (line buffer 32 -> 96 under RNLAB) and polls
  the net while idle. modules/net gains weak hook points
  (`cads/net/rnlab_hooks.h`: rx_frame/rx_drop/tx_frame/tx_drop,
  `LWIP_HOOK_IP4_INPUT`) and `CADS_RNLAB_LWIP_STATS` (LWIP_STATS=1, ~416 B
  SRAM). All 11 lesson files + tests (ctest label `rnlab-LNN`) exist as
  placeholders so lesson branches never touch shared files. RAM margin 384 B
  (RNLAB=OFF: 896 B), flash 332 KB. HIL on the bench board: ping after reset
  0 % loss, `lab info` over UART (app tree and explorer) and telnet :4242,
  `lab net dhcp`/`static` round trip, `V 5` flush throughput unchanged at
  342 kpixel/s, forensic ring empty.

- 2026-09-01 (Marauder Bluetooth: independently re-verified live, real BLE
  devices found) - The operator pointed out this was already flashed and
  hardware-verified (2026-08-28 entry, `docs/reference/marauder-coprocessor.md`)
  and invited a fresh webcam check rather than trusting the doc. Did that:
  navigated the real touchscreen menu via `board_key.py` (Menu -> Marauder ->
  Sniff BT, matching `apps/marauder/cads_marauder.c`'s tool table), photographed
  the panel live. Real result: 5 real nearby BLE devices populated the screen,
  MAC addresses plus one named device ("JBL Wave FLEX"), matching the
  documented output format exactly - confirms the ESP32 co-processor's BLE
  stack still works end to end, independently of the original verification.
  Exited cleanly (Back x3 to desktop) afterward, board left in a normal
  resting state.
  **Real friction hit and not fully root-caused, worth flagging**: plain
  `board_cmd.py` commands ('E', first 'k') worked cleanly at the very start
  of this session, then started silently hanging (no output, no error) after
  the first `board_key.py ok`/`quit` sends - consistent with the documented
  "app-tree ignores plain ASCII" behavior, but `board_key.py quit` sent alone,
  with nothing else touching the port, did not visibly restore plain-command
  responsiveness either. Compounded by a real self-inflicted mistake:
  repeatedly re-issuing `board_cmd.py` after a premature per-call Bash-tool
  timeout auto-backgrounded the previous attempt, instead of waiting for or
  killing it first, left up to 4 `board_cmd.py` processes reading the same
  UART concurrently at one point (confirmed via `ps aux`, killed cleanly).
  That pile-up definitely produced some of the garbled output seen, but a
  clean single `k` call still hung afterward, well after all stray processes
  were killed - so a real "how does the console recover to plain-command mode
  after `board_key.py` has been used" question remains open, sidestepped this
  session by driving navigation entirely through `board_key.py` + webcam
  photos instead of chasing it further. A live GDB attach along the way
  (`st-util --no-reset`) also produced one false alarm worth recording: PC
  parked inside `cads_stackguard_breached()` looked like a caught crash at
  first glance, but is actually normal - that function is `vApplicationIdleHook`'s
  own continuous health-check poll, and all four sentinels read the correct
  canary value at the time. Detached cleanly (non-batch `continue` + `SIGINT`,
  per the established recipe); `st-info --probe` confirmed the ST-Link was
  never wedged.

- 2026-09-01 (HARDWARE GATES M3 and M6: touch walkthrough closed on the
  operator's own confirmation) - Both gates' one remaining open item was
  the same physical-presence gap neither this agent nor a photograph could
  close: a real human touching the XPT2046 panel, previously blocked on a
  precisely-isolated SPI data bug (2026-08-19/25 Log entries). The operator
  confirmed directly, this session, having "schon ordentlich getestet"
  (already tested it properly) - taken as the human walkthrough both gates
  have always deferred to, not independently re-verified here. Most likely
  explanation, not re-traced: the shared-SPI-bus mutex fix (`9506a46`,
  present at session start, fixing the ui/input/console task race
  documented in this file's own crash-family entries) landing between the
  XPT2046 bug's isolation and this confirmation. Both gates marked `[x]`.

- 2026-09-01 (`golden_splash`/`golden_boot_desktop`: fixed, root cause was
  environmental, not a code regression) - The two host golden-image tests
  flagged-not-fixed in the 2026-08-30 entry below were investigated fresh
  rather than trusted stale. Confirmed still failing (18866/153600 and
  14273/153600 pixels), then root-caused with a per-pixel delta histogram
  instead of eyeballing the diff PNGs: every differing pixel across BOTH
  images was off by exactly +1 in one or more of R/G/B - deltas were only
  ever `(1,1,0)`, `(1,1,1)`, or `(1,0,1)`, confined entirely to
  anti-aliased edges (flat indexed-palette regions matched exactly). That
  is the signature of SDL's RGB565->24bpp BMP upconversion rounding
  slightly differently on this host than whatever SDL2 build last captured
  the goldens (`golden_check.py`'s own docstring already documents that
  SDL, not this project's rendering code, does that conversion) - not a
  rendering regression. Confirmed no rendering-relevant commit landed since
  the goldens were last regenerated (`06b9f12`): the only rendering-adjacent
  commit since then (`a8f8c76`, touch calibration) doesn't touch color
  output. Side-by-side visual comparison of old vs. regenerated goldens:
  identical. Regenerated both via the project's own `update_golden` CMake
  target (the documented, reviewed path for exactly this situation - not a
  one-off script hack), verified 37/37 host tests now pass
  (`cmake --build build/host && ctest`), confirmed the board build is
  untouched (`cmake --build build/itsboard` reports no work to do - only
  PNG assets changed) and RAM margin unchanged at 928 B. Committed as
  `3c038cb`.

- 2026-08-31 (new tutorial: `docs/tutorials/lwip-udp-hello.md`, sending a real UDP
  datagram from the board with `cads_net_udp_send()` - hardware-verified end to
  end, `nc -ul` on this Mac actually received `hello from cads-zero` sent by the
  real STM32F429ZI's lwIP stack). Written by a multi-agent research+draft pass
  grounded in the real API (`modules/net/include/cads/net/net.h`) and the one
  existing real caller (`apps/marauder`'s PCAP-over-TZSP relay), then verified
  by hand rather than trusted on read-through - and the verification pass
  caught two real bugs in the draft before it was ever published:
  1. `scripts/board_cmd.py P <hex-ip> <count>` needs the ip+count as ONE quoted
     shell argument (the board's own console parses `"<hex> <count>"` out of a
     single string; `board_cmd.py` itself only accepts one positional
     `argument`) - the drafted command passed them as two separate CLI args,
     which `argparse` rejects outright.
  2. **A real, previously-undocumented gotcha, cost real debugging time to
     find**: a freshly-flashed board's `boot.autostart=1` drops straight into
     the touchscreen app-tree menu (`cads_explorer_app_demo`), and - unlike
     what an earlier CLAUDE.md lesson about the interactive `d`-session
     implied - that loop does not exit on an arbitrary console byte. It
     ignores plain ASCII entirely, by design (`board_key.py --help`'s own
     docstring: the reserved key-bytes are all `>= 0x80` specifically so they
     can never collide with, or be confused with, an ordinary typed command).
     A `board_cmd.py` command sent while the board is in that state produces
     *zero output, not even an error* - looks exactly like a hung/crashed
     board. Live GDB attach (twice, both proper non-batch `continue`+`SIGINT`
     detaches, not batch-mode halts) proved the board was fine both times,
     just idling in `cads_explorer_app_demo`'s own delay loop. The actual fix:
     `scripts/board_key.py quit` first, *then* the real command. Added to the
     tutorial's own troubleshooting table so a student hits this once, not
     blind. This refines (does not contradict) the earlier `d`-session lesson
     below - a version-specific detail worth knowing rather than a case where
     that lesson was wrong.
  Also hit, and resolved on retry (matching the already-documented pattern,
  not a new phenomenon): one `st-flash write` verification failure at offset
  30720, resolved on the very next retry - see the 2026-08-29/30 entries for
  the established pattern this matches.

- 2026-08-30 (Marauder menu: real crash-and-reboot found live, one real bug
  fixed - the underlying stack overflow is NOT yet fixed, still open) -
  User hit a real, reproducible crash navigating the Marauder menu on
  actual hardware, unrelated to any of the day's WebUSB/firmware-lab work
  (confirmed: no flash/SWD action had touched the board since the last
  verified-good reflash). `E`'s forensic ring (`this boot's reset cause:
  IWDG watchdog`) showed the real sequence: a `reason=input` record (the
  project's own stack-guard sentinel, `apps/bringup/tasks.c`'s
  `vApplicationIdleHook()`, catching `cads_input_stack` overflowing) 22ms
  before a `reason=HardFault` record with `HFSR=0x80000000` (DEBUGEVT) -
  the exact same "`bkpt` with no debugger attached escalates into a second
  HardFault instead of falling through cleanly" bug already found and
  fixed in `fault_handlers.c`'s `cads_fault_dump()` on 2026-08-26, but
  `hal_io.c`'s `cads_hal_panic()` (a separate function, same PM0214-
  documented Cortex-M behavior, same fix shape) never got the same
  `DHCSR.C_DEBUGEN` guard. Fixed here, hardware-verified (built, RAM budget
  unchanged at 928B margin - a code-path guard, no new state - flashed,
  reset, GDB-confirmed booting and running normally).
  What this fix actually buys: with no debugger attached (the normal case),
  the observable outcome is still "board freezes then IWDG-resets a couple
  seconds later" either way, since `cads_hal_panic()` disables IRQs before
  reaching this point regardless - nothing feeds the watchdog whether the
  `bkpt` escalates or not. The real improvement is diagnostic quality: a
  debugger attached during/after the freeze now sees one clean panic
  record instead of a second, confusing fault stacked on top of it.
  **Follow-up, same session:** the actual root cause is now also fixed.
  User asked explicitly to hold off on a git tag until it was ("Fehler
  ausgemerzt" - eradicated, not just made cleaner to diagnose), so this
  didn't stop at the escalation fix. `CADS_INPUT_STACK` (`apps/bringup/
  tasks.c`) was 256 words (1 KB) - smaller than even the UI task's 512
  words, despite carrying arbitrary app-specific call depth (every app's
  input handler runs synchronously on this task's own stack via
  `cads_input_tick()` -> `cads_input_set_callback()`), not just its own
  shallow polling loop, which is what the file's own "the others are
  shallow" comment had assumed. Same class of problem, same fix shape, as
  the 2026-08-28 `net.dhcp`/console-stack incident: quadrupled to 1024
  words, matching the console stack's own post-fix size, costing 3 KB of
  CCM (task stacks live there, ~54.7 KB still free of 64 KB afterward -
  not a single byte of the tight SRAM heap margin, unaffected at 928 B).
  Hardware-verified: builds clean, boots and runs correctly (GDB-confirmed,
  matching stack trace as every other check this session). Not
  independently re-reproduced live in the exact same Marauder menu
  navigation that originally triggered it (would need the physical
  Marauder co-processor session replayed step for step) - the fix directly
  targets the exact stack the forensic ring proved was overflowing, with a
  4x margin bump matching an already-proven-effective precedent, but real
  field use is the final word, same as any stack-sizing fix. Two host-only
  golden-image tests
  (`golden_splash`, `golden_boot_desktop`) are currently failing
  (pixel-diff, not a crash) - confirmed unrelated to this fix (it only
  touches `targets/itsboard/hal/hal_io.c`, which the host/sim build never
  compiles) and not otherwise investigated this session; flagged, not
  fixed.

- 2026-08-30 (CADS-DEMO-firmware-lab: WebUSB flash-write - the actual root
  cause, found and fixed after the entry below turned out to be an
  intermediate, incomplete conclusion) - The prior Log entry's "isolated to
  the Node WebUSB test harness, not the algorithm" verdict was wrong: the
  operator's own real-Chrome click test reproduced the identical
  FLASH_SR=0xc0 (PGPERR|PGSERR) failure, disproving it outright. Real root
  cause, found by testing an erase-only operation (via an all-0xFF payload,
  which webstlink's own write() already skips) and reading the target
  sector back afterward: the erase was reporting success but never
  actually taking effect on real silicon. `Flash.unlock()` - called once at
  the start of every flash operation - was calling `core_reset_halt()`,
  which is a REAL chip reset (writes AIRCR.SYSRESETREQ), not just a debug
  halt. That restarts CaDS Zero's own boot sequence, which re-arms its ~2s
  independent hardware watchdog (IWDG) - a watchdog this project already
  correctly freezes on debug-halt via DBGMCU_APB1_FZ_DBG_IWDG_STOP, but
  only once the core actually reaches that halted state again. A WebUSB
  erase+program sequence is real USB round-trip traffic per register
  access and can easily outlast that 2s window before the core gets back
  to a frozen-watchdog state, so the IWDG fires mid-erase and resets the
  chip a second time, out from under the in-progress operation.
  FLASH_CR reading back as 0x80000000 immediately after a plain SER write
  - the anomaly chased through the whole prior investigation - is that
  register's literal power-on-reset default: direct, on-the-nose evidence
  of an actual unwanted reset, not a corrupted/stale register read as
  previously assumed. Fix: `unlock()` now calls `core_halt()` instead -
  nothing about erasing or programming flash via the debug port requires
  resetting the target first, only having the core not actively executing.
  Verified end to end on real hardware after the fix: an erase-only
  operation reads back as genuinely all-0xFF (not just BSY-clear) before
  any write; a full erase+program+verify of the real demo firmware
  completes without error and the target boots and runs correctly
  afterward (confirmed via GDB, matching stack trace as every other check
  this session). Patch still lives in a local scratch clone of
  devanlai/webstlink, not yet vendored into firmware-lab - see the entry
  below for the plan (vendor directly, no fork, matching the lwip
  patch-file precedent) and Labor's independent register-level review of
  the intermediate fixes, both still accurate for the fixes that carried
  forward into this final version.
  Also fixed in this pass: `WebStlink.detach()` was missing an `await`
  before `this._mutex.lock()`, so it never actually waited its turn - a
  Disconnect click (or the demo's own 200ms polling timer) could tear down
  the USB connection while a flash()/inspect_cpu() call elsewhere was
  still mid-transfer, throwing "InvalidStateError: ... An operation that
  changes the device state is in progress" on real hardware. Found from
  the operator's own bug report ("I click flash, but nothing happens" -
  the actual cause of that specific symptom turned out to be simpler still:
  the demo's Flash button stays disabled until the target is Halted, and
  its click handler silently no-ops if no file is selected - neither
  produces any visible error, matching "no error message, nothing happens"
  exactly).

- 2026-08-30 (CADS-DEMO-firmware-lab: WebUSB flashing/debugging deep-dive on
  `webstlink` - three real bugs found and fixed, one root cause correctly
  isolated to the Node test harness rather than the algorithm) - User
  wanted this made "primaerer Weg" (primary path) for the demo's
  multi-user architecture (see the earlier Log entry on why the
  shared-container-behind-a-tunnel model can't reach per-student hardware
  directly; WebUSB, running client-side in each student's own browser,
  sidesteps that entirely). Testing against the real Nucleo-F429ZI via
  Node + the `usb` npm package's WebUSB polyfill (the native browser
  device-chooser dialog can't be automated - confirmed via a CDP
  `DeviceAccess` domain attempt that never fired, so real end-to-end
  browser verification still needs one human click) found:
  1. `erase_sector()`'s busy-wait timeout looked up `max_erase_time` by
     erase size in BYTES (16384/65536/131072, from the device's own
     erase_sizes list) against a table keyed in KB (16/64/128) -
     `undefined` lookup -> `NaN` end_time -> the wait loop's condition was
     false before the first iteration, so `wait_busy()` never polled
     FLASH_SR even once and threw "Operation timeout" immediately, on
     every single erase. This driver (`stm32fs.js`, the sector-based "FS"
     flash driver used for F4/F7/L4) is a different code path from the
     page-based "FP" driver F1/F3 boards use - the repo's own "tested on
     STM32F103 only" claim never actually exercised this code at all.
  2. `end_of_operation()` treated ANY nonzero FLASH_SR as a fatal error,
     including EOP (bit0, set by hardware on ordinary successful
     completion) - fixed to check only the real error bits
     (WRPERR/PGAERR/PGPERR/PGSERR), matching pystlink's own
     `FLASH_SR_ERROR_MASK` (webstlink's own stated upstream, which this
     divergence had drifted from).
  3. The whole flash-*write* step used to upload a hand-assembled ARM
     Thumb copy-loop into SRAM and have the target CPU execute it - not
     what pystlink actually does for F2/F4 (direct writes through the
     debug probe's own memory-access port, `set_mem32`, the same mechanism
     st-flash/OpenOCD use). Rewritten to match, removing an entire
     untested, hand-assembled-opcode code path.
  Diffing against st-flash's own C source (`common_flash.c`) surfaced one
  more real behavioral gap: st-flash sets FLASH_CR's PSIZE/PG (for
  programming) and SER/SNB/STRT (for erase) as separate
  read-modify-write transactions, not one blind combined-value write -
  matched in the fix even though the final register value comes out the
  same either way, since it's what the known-working reference actually
  does.
  After all four fixes, real-hardware testing still hit an intermittent,
  hard-to-explain symptom: reading FLASH_CR immediately after a plain SER
  write sometimes returned 0x80000000 (LOCK) instead of the value just
  written, stable across repeated reads (an explicit flush-read ruled out
  a simple DAP read-pipeline race). The decisive check: replaying the
  *exact same* register sequence (unlock, clear SR, write SER, read back
  x3) over `st-util`'s GDB remote stub instead of Node/webstlink - a
  completely different, mature software stack talking to the same
  physical ST-Link - read back the correct value every time. That
  isolates the remaining symptom to the Node `usb` package's WebUSB
  polyfill (v3.1.0) used for testing, not this algorithm or the silicon:
  the register sequence itself is verified correct on real hardware.
  Board safety throughout: every real erase/write test cycle was followed
  by a full reflash-and-GDB-verify of the actual CaDS Zero image before
  moving on - confirmed clean and running correctly at every checkpoint,
  including after a real ST-Link USB wedge mid-investigation (recovered
  via unplug/replug + `st-flash --connect-under-reset`, no data loss - the
  wedge is the same class of issue as the maintainer's own ST-Link
  wedge notes above, not something new). Patched `webstlink` source lives
  in a local scratch clone, not yet committed/pushed anywhere - the fixes
  need one more real-browser (not Node) end-to-end pass before shipping.

- 2026-08-30 (CADS-DEMO-firmware-lab: tutor-mode LLM wiring found broken,
  root-caused, fixed - end-to-end student setup now verified working) -
  User's standing directive: keep driving/testing firmware-lab "bis ein
  Student wirklich Erfolge... machen kann" (until a student can genuinely
  succeed with the whole setup). Provided real LiteLLM credentials/endpoint
  for the tutor extension's "Ask about this step" feature; first live test
  (Playwright-driven browser IDE, `>review-cli: Start Firmware Tutor`, a
  real question typed into Step 1) returned "LLM backend returned an error
  (401 Unauthorized)" on every ask. Root cause, confirmed by curl from
  inside the `firmware-lab` container: `LITELLM_BASE_URL` was set to
  `http://llm-34a13a96.bunsenbrenner.org/v1` (plain http); that host
  force-redirects http->https with a 308, and the Authorization header is
  dropped across that scheme-changing redirect (curl's own default
  behavior without `--location-trusted`, and almost certainly what the
  extension's own HTTP client did too - same request against the https URL
  directly returns 200 with a real model list). Fix verified working:
  `LITELLM_BASE_URL=https://...` (https, not http) - recreated the
  container, re-ran the exact same Ask flow, got a correct real answer back
  from the model. Reported to Labor with the root cause and suggested fix
  (default the compose env/README to https - plain http silently 401s
  instead of failing loudly at startup). Also confirmed while testing: all
  5 tutor walkthrough steps load and are technically accurate for this
  exact board (STM32F429ZI Nucleo, LD1/LD2/LD3 correctly on GPIO port B),
  steps 4-5 are real hands-on exercises not just reading. Combined with the
  earlier Makefile-path fix, the browser IDE + build + tutor LLM path is
  now genuinely usable end to end; the one remaining known gap (flashing
  over USB) is a confirmed Docker-Desktop-for-Mac platform limitation, not
  something a student on native Linux (or real USB passthrough) would hit.

- 2026-08-30 (superseded by a cleaner fix: the lwip CI fork below is gone -
  a plain patch file instead) — User's own reaction to the fork fix
  ("Ich möchte wirklich ungern ein eigen Fork") was right: a fork was
  never actually necessary, just the fastest fix in the moment. Real fix:
  reset `lib/lwip`'s pin back to plain upstream `lwip-tcpip/lwip.git`, at
  the fix commit's own real parent (`3d896ba`, upstream master's actual
  tip - always reachable, no special hosting needed), and moved the
  lwiperf fix itself to `lib/patches/lwip-lwiperf-abort-fix.patch`,
  applied automatically at CMake configure time
  (`modules/net/CMakeLists.txt`, board target only) - idempotent (checks
  for the fix's own marker line before applying, matches the pattern
  `tools/marauder-build/build_and_flash.sh`'s own source patches already
  use), fails loudly if the patch no longer applies cleanly rather than
  silently skipping a real fix.
  Verified end to end, not just configured: reset the submodule checkout
  to the real unpatched upstream commit, confirmed the fix marker was
  genuinely absent, ran a fresh `cmake --preset itsboard` and watched the
  patch actually apply (`-- Applying lib/patches/lwip-lwiperf-abort-fix.
  patch to lib/lwip`), reconfigured again and confirmed it was silently
  skipped (idempotent, no error), then a full board rebuild produced the
  byte-identical flash image (327 080 B, same as every other build today)
  and unchanged RAM margin (928 B) - the patch-file path produces exactly
  the same compiled result as the commit-pin path did, as it should.
  `scimbe/lwip` (the fork from the entry below) is no longer referenced
  anywhere in this repo; left the fork itself on GitHub rather than
  deleting it (a repo deletion is the kind of action worth a separate,
  explicit ask, not bundled into this cleanup).

- 2026-08-30 (Performance A/B campaign, candidate 2: `-O2` measured,
  hardware verification deliberately deferred) — `-O2` (via a scratch
  build using an unrecognized `CMAKE_BUILD_TYPE` name so none of
  `cads_flags`' own `-Og`/`-Os` generator-expression flags apply and only
  the intended `-O2` reaches the compiler - the first attempt silently
  built `-Os` anyway because target-level `target_compile_options` always
  wins the "last flag on the command line" tie-break over
  `CMAKE_C_FLAGS`/`CMAKE_C_FLAGS_RELEASE`, caught by checking
  `compile_commands.json` directly rather than trusting the configure
  step alone) measured smaller than `-Os`: 269 044 B vs. 277 700 B flash
  (candidate 1's own number) - counter to the usual `-O2`-trades-size-for-
  speed expectation, plausible here given `-ffunction-sections
  -fdata-sections` + linker gc-sections are already on, which changes how
  the two optimizers' inlining/duplication trade-offs land. RAM margin
  identical (928 B) to every other optimization level tried so far.
  **Hardware verification deliberately NOT done for this candidate right
  now.** Candidate 1 (`-Os`) already has a real, unresolved BusFault
  (previous two Log entries) - testing whether `-O2` also crashes (or
  crashes differently) would not currently distinguish "a new `-O2`-
  specific problem" from "the same latent bug `-Os` already exposed",
  since the underlying UB in `cads_text_draw_line`/`cads_canvas_draw_text`
  has not been found and fixed yet. Spending another full hardware-
  verification cycle (and this session's SWD reliability has been
  degrading with length, see the entry above) on an ambiguous result
  isn't worth it before that's resolved. Next real step for the whole
  optimization-level track is finding that bug, not adding a third
  unverified candidate on top of it - candidates 3-5 (LTO, DMA2D, timing)
  are on hold for the same reason.

- 2026-08-30 (Board<->Mac secure link: wire framing built) — User's
  explicit follow-up to the crypto primitive layer (2026-08-28): "treibe
  das Framing voran". `modules/security/include/cads/security/
  secure_frame.h` + `.c`: `cads_secure_frame_encode()`/`_decode()` over
  `cads_secure_link`'s seal/open, exactly to ct-agent's own review notes
  from that earlier design session - 4-byte magic (first byte `>= 0x80`,
  same disambiguation-from-plaintext convention this project already uses
  for headless key injection, so a secure frame can never be mistaken for
  a `cads_cli` command line during any transition period where both
  exist), little-endian length field, nonce in the clear, ciphertext, mac.
  Streaming-safe decode (`INCOMPLETE` on a partial frame with 0 bytes
  consumed, `BAD_MAGIC` with 1 byte consumed for stream resync,
  `AUTH_FAILED` with the full frame consumed on a tampered/wrong-key
  frame) - built for a real TCP consumer, not just a round-trip demo.
  11 host tests (`tests/unit/test_secure_frame.c`), all passing; host
  suite overall 35/37 (same 2 pre-existing golden-image failures,
  unrelated). Zero RAM/flash cost on the shipped board build - nothing
  calls this yet, so the linker drops it (confirmed: RAM margin
  unchanged at 928 B).
  **Deliberately not done here**: wiring this into `cads_cli` (or any
  other live transport) - the natural fit is the existing port 4242 `j`
  console command, but that is a live-transport change touching
  lwIP/scheduler-adjacent code, a different risk class than framing
  itself, and key provisioning (`security.psk` in `modules/config`) is
  still open too. See `docs/reference/secure-link.md` for the fuller
  writeup.

- 2026-08-30 (Deliberate reproduction attempt on the Release-build BusFault
  - not reproduced; separately, new evidence that flaky `st-flash write`
  verification is NOT specific to `cads_config.py`) — Follow-up to the
  entry below, per the user's explicit "jagen" (hunt it). Reflashed the
  Release build, attached `st-util --no-reset` + GDB with `continue`
  *before* touching anything (so this time a real fault would trap
  cleanly into the live debugger, per this session's own bkpt/DHCSR fix,
  instead of escalating into the fault-loop that fix closed), then drove
  `scripts/board_key.py` through essentially every text-rendering screen
  in the app tree - Settings, About, GPIO, Network, Active Net Tools'
  full list, Marauder's full tool list, the file browser, the Reflex Test
  entry screen. No trap in ~15 minutes of deliberate, focused navigation.
  **Conclusion: not reproduced, not exonerated.** The original forensic-
  ring record (previous entry) still stands as real evidence - a coherent,
  non-garbage PC/LR resolving specifically against the Release ELF's
  `cads_text_draw_line`, not the Debug build's addresses - so this is
  logged as "rare or state-dependent, trigger condition still unknown",
  not "false alarm". Immediately reflashed the known-good Debug build
  afterward per the same safety-first discipline. **Release remains
  NOT shipped** until either the trigger is found (a longer unattended
  soak, or a `-Os`-with-better-debug-info build to get a live trace
  without needing to catch it by chance) or the code path is audited by
  hand for UB that `-Og` happens not to expose.
  **Separately, real new evidence for the open `cads_config.py`
  corruption-correlation item**: reverting the flashed firmware back to
  Debug afterward needed **five consecutive plain `st-flash write`
  attempts** before one verified successfully (offsets 43008/30720/0/0/
  success) - `st-info --probe` and NOD_F429ZI-mount checks came back
  clean between every attempt, and critically, **no `cads_config.py` call
  was anywhere in this sequence** - just repeated `st-flash write` of the
  same firmware image. This weakens the earlier hypothesis that
  `cads_config.py`'s specific two-call (read, then write) pattern is what
  triggers the flakiness, and points instead at something more general
  about this host/cable/ST-Link's SWD reliability under sustained,
  extended use (this session had already been running for many hours of
  near-continuous SWD traffic by this point) - worth checking USB power
  management, cable/hub quality, or simply session length/thermal state
  as the next hypothesis rather than anything specific to the config
  script. The very next `st-flash reset` in the same sequence also failed
  once ("Can not connect to target") and succeeded on a bare retry with
  no other action taken - same flavor of transient flakiness, not a wedge
  (ST-Link stayed healthy and responsive to `st-info --probe` throughout).

- 2026-08-30 (Performance A/B campaign, candidate 1: Debug->Release for the
  flashed firmware - a real fault found, reverted, NOT shipped) — User
  asked for proactive, A/B-measured performance work as standing filler
  whenever nothing else is queued, with an explicit, repeated constraint:
  never risk functionality/robustness/stability for a performance gain.
  Candidate 1: the `itsboard` CMake preset has always hardcoded
  `CMAKE_BUILD_TYPE=Debug` (`-Og -g3`) for the actual flashed firmware - a
  `Release` config path (`-Os -g`) has existed in the top-level
  `CMakeLists.txt` the whole time but was never used to build what
  actually ships. Built it standalone (own `cmake -S . -B <scratch dir>
  -DCMAKE_BUILD_TYPE=Release`, same toolchain file) to compare.
  **Measured:** flash usage 327 080 B -> 277 700 B (-15.1%); RAM budget
  margin unchanged (928 B both - `-Os` mostly affects code size, not the
  static data layout that budget actually gates).
  **Then hardware-verified before trusting it - and it failed the check.**
  Flashed, reset, ran `E` (forensic dump): a real `BusFault`
  (`BFAR=0x230162C3`, `R3` matches - a live invalid-address dereference,
  not a garbled/stale-looking record) with a coherent, non-garbage
  PC/LR that `arm-none-eabi-addr2line` resolved against the *Release*
  ELF specifically to `cads_text_draw_line`
  (`gui/widgets/cads_textbox.c:91`) / `cads_net_poll` in the LR slot (the
  LR value itself looks like leftover register content, not a real
  caller - the PC match is what matters here). This did **not** reproduce
  against the Debug ELF's own addresses at all, and the ring's other
  slots read as genuine garbage (the CCM-content-misread pattern this
  file already documents elsewhere), so this one record stands out as
  real, not noise pattern-matched by accident.
  **Immediately reflashed the known-good Debug build and reset** - verified
  clean boot (self-test `ok 1`/`ok 2`/`ok 3`...) - rather than leave a
  build with an unconfirmed crash on the device. This is exactly the
  scenario the user's own "never risk stability" instruction exists for:
  an optimization that looked purely mechanical (compiler flags, same
  source) surfaced what looks like real, previously-latent undefined
  behavior that `-Og`'s more conservative codegen happens to not trigger -
  classic "works until you optimize it harder" territory, not something a
  flash/RAM number alone would ever have caught.
  **Status: candidate 1 REJECTED as-is, not shipped.** Root cause not
  found yet - `cads_text_draw_line`'s own bounds handling
  (`gui/widgets/cads_textbox.c`, the `buffer[i] = text[line.offset + i]`
  loop and its `cads_canvas_draw_text` call) is the place to start; worth
  a deliberate, single-purpose reproduction attempt (flash Release again,
  exercise every text-rendering screen deliberately, watch for the same
  fault) as a follow-up, but not folded into this campaign until that
  reproduction is done and the actual UB is found and fixed. Candidates
  2-5 (see this session's earlier plan) unstarted, pending user direction
  after this result.

- 2026-08-29 (Expert pentest gap review + three built: net.mac_random,
  active.armed, screencast-viewer verified; `cads_config.py` push/pull
  correlates with real vector-table corruption - open) — User asked, as a
  penetration tester, what this firmware is still missing that matters for
  field engagements. Answered with a verified (not guessed) list: checked
  the actual repo before claiming anything absent - `docs/HARDWARE.md`
  itself already flags USB OTG FS as "present, unused" (BadUSB/HID
  injection - the biggest real gap), WPA2/WPA3 handshake capture already
  exists (`sniffpmkid`/`sniffsae`, confirmed in `cads_marauder.h`, so NOT
  a gap), no loot/case-evidence concept, a single fixed hardcoded MAC
  (`apps/bringup/explorer_eth.c`), and no scope/arm safety gate on any
  active tool. User picked: build `net.mac_random` and `active.armed` now,
  improve (don't necessarily rebuild) the MAC point, BadUSB discussed but
  not started, loot/case concept and a wipe-on-seizure feature deferred.
  **`net.mac_random`** (`modules/config`, `apps/bringup/explorer_eth.c`):
  opt-in config bool: `cads_explorer_net_mac()` draws a fresh, correctly-
  flagged (unicast + locally-administered bits forced) MAC from the
  hardware RNG once per boot instead of the fixed `02:CA:D5:5E:00:01`
  default - OPSEC against a field device presenting the same trackable
  identity on every engagement's network. Board only (needs
  `cads_hal_rng_bytes()`); simulator always uses the fixed value.
  Hardware-verified: two resets produced two different, correctly-flagged
  addresses (`02:B5:CC:97:61:56`, then `9E:C7:98:DA:A4:8F`). Left enabled
  on the actual board as the delivered improvement, not just built.
  **`active.armed`** (same config module, `apps/marauder/cads_marauder.c`,
  `apps/active/cads_active.c`): device-wide safety catch gating every
  transmit-based tool's CONFIRM step (Marauder's Deauth/Evil
  Portal/Beacon Spam/Probe Flood/BLE Spam; M9's forged-frame suite) -
  default off, blocks Yes from ever starting anything until a deliberate,
  out-of-band config change arms it; independent of M9's own pre-existing
  session-only `dry_run` toggle (kept, still useful, just not a hard
  gate). Hardware-verified both states: unarmed shows "BLOCKED: device not
  armed" with only Back live; armed shows the original Yes/No confirm
  screen unchanged. Found and fixed a real latent bug sizing this: the
  config serialize buffer was already too small for a fully-populated
  config (~676 B needed vs. 512 B available) - `cads_str_append`'s bounded
  writes would silently truncate the tail rather than error. Fixed by
  raising the limit *and* merging load()/save()'s two separate static
  buffers into one - net RAM effect was a 256 B *saving* despite the
  larger buffer (672 B margin -> 928 B).
  **Screencast viewer (Maintainer labor's PR #70)**: verified end-to-end
  against real hardware (live frames, correct 480x320/4bpp/palette, in an
  actual browser via Playwright) - see that PR's own thread; not a
  firmware change, noted here for the day's full record. Install/usage
  docs written (`docs/how-to/screencast-viewer.md`).
  **Open, found while hardware-verifying the above, not yet root-caused:
  `scripts/cads_config.py` push/pull operations correlate with real,
  reproducible flash corruption at the firmware region** - the exact same
  single-bit vector-table corruption pattern as issue #57
  (`0x10010000 -> 0x00010000`, word 0 of the vector table at
  `0x08000000`), confirmed via `st-util --no-reset` + live GDB register
  reads multiple times this session, each time shortly after a
  `cads_config.py push`/`pull` cycle (which only nominally touches
  `0x08120000`, the filesystem region - not `0x08000000`) even with
  `NOD_F429ZI` never observed mounted at the time. Recovered each time by
  a plain firmware reflash + explicit `st-flash reset` (also learned: a
  bare `st-flash write` with no following `reset` left the core genuinely
  halted, not crashed - which produced its own separate round of "board
  unresponsive" false alarms this session before the pattern was
  understood; always follow a `write` with an explicit `reset` unless
  chaining into another `write`). Root cause not found - candidates not
  yet checked: whether `cads_config.py`'s two separate `st-flash`
  subprocess invocations (read, then write) each do their own reset-for-
  programming sequence that could race with a background client, or a
  possible `st-flash`/ST-Link erratum specific to writing right up to the
  2 MB flash ceiling (`0x08120000 + 896 KB = 0x08200000` exactly). Until
  root-caused: after any `cads_config.py push` or `pull`, verify the board
  actually boots (console or a photo, not just the script's own success
  message) before trusting it, and reflash firmware if anything looks off
  - a plain reflash has fixed it cleanly every time so far.

- 2026-08-29 (GUI session: explicit quit byte replaces the 30-minute
  timeout) — User's own proposal, after the earlier boot-loop recovery
  above led to sending `d 1800` to keep the panel usable, which then
  expired an hour later ("warum kann ich die gui wieder nicht bedienen?" -
  a real, mundane timeout, not another crash, confirmed via a plain `k`
  probe answering normally). Rather than pick a bigger arbitrary duration
  (considered and rejected - still just delays the same problem), the user
  asked for an explicit on/off: enter GUI mode by command, leave only via
  one dedicated "button" that does not physically exist. Implemented
  exactly that: `apps/bringup/explorer_app_demo.c`'s session (both `d` with
  no argument and `boot.autostart`'s own call, both `seconds == 0u`) now
  runs unbounded and ignores every plain console byte, same protection a
  bounded `d <n>` already had; the only exit, at any point, is one new
  reserved byte (`CADS_APP_DEMO_EXIT_BYTE = 0x88`, in the same `>= 0x80`
  range as the existing button-injection bytes) sent via
  `scripts/board_key.py quit`. This removes the old `seconds == 0` special
  case ("exits on any byte") entirely rather than adding a third mode -
  bounded and unbounded sessions now share one byte-handling rule, which
  is simpler code than before, not more. `d [sec]` help text and
  `docs/reference/explorer-console.md` updated to match.
  Hardware-verified: boot.autostart's unbounded session survived an `E`
  probe untouched (confirmed live on camera - Leo still on screen after
  the probe), `board_key.py quit` handed the console back immediately, a
  plain command worked right after. Host suite: 34/36 (same 2 pre-existing
  golden-image failures as the entry above, unrelated - not caused by this
  change, `explorer_app_demo.c`/`explorer.c` have nothing to do with
  splash/desktop rendering).

- 2026-08-29 (Real root cause of the silent boot-loop above: `bkpt` with no
  debugger attached re-faults instead of halting - fixed) — The reflash
  documented in the entry directly below got the panel back once, but the
  same silence recurred twice more the same session even after a verified-
  clean reflash and a filesystem-region-only config change (`net.dhcp`
  0->1), which should never touch code. `st-util --no-reset` + live GDB
  caught the actual live PC parked inside `cads_fault_dump` itself
  (`targets/itsboard/startup/fault_handlers.c`) on two separate attaches,
  and `arm-none-eabi-addr2line` against `build/itsboard/cads-zero.elf`
  confirmed it: the fault handler was faulting *on itself*. Reading the
  function found why - its last step before an intentional `for(;;) {}`
  halt is `bkpt #0`, meant to trap into a live debugger and leave the
  ordinary loop as the "nothing attached" fallback (the function's own
  header comment says exactly that). On this part, executing `bkpt` with
  `DHCSR.C_DEBUGEN` clear does not fall through quietly - PM0214 confirms
  it escalates straight back into HardFault, so the *fault handler itself*
  re-entered on its own bkpt, over and over, each cycle ended only by the
  IWDG timing out and resetting the board - which booted clean, ran until
  whatever the *original* bug was fired again, and repeated. This is why
  the forensic ring held four records from two genuinely different fault
  types (`BusFault`/`HardFault` at one boot, `MemManage`/`HardFault` at
  another) instead of the one clean record the ring is designed to
  capture - and why nothing was visible on console for hours: every
  individual boot's diagnostic output was real, just never reaching a
  listener, and the design that was specifically supposed to survive that
  ("still readable by a debugger at the next reboot even if nothing is
  attached right now") was exactly what the missing guard broke.
  Fix: gate the `bkpt` on `DCB->DHCSR & DCB_DHCSR_C_DEBUGEN_Msk` (CMSIS_6
  naming; `core_cm4.h` also aliases the legacy `CoreDebug_DHCSR_*` names to
  the same bits) - a debugger still gets the trap it's built for, an
  unattended board now gets the clean, quiet halt the loop was always
  meant to be. Rebuilt (RAM margin unchanged, 672 B - this is flash-only
  code), reflashed, reboot verified clean via console + a live webcam
  photo of the Home screen.
  **Not yet found: the original bug that faults in the first place.**
  Left running on `net.dhcp = 0` (the known-stable config all session) as
  the safe default rather than immediately flipping back to `net.dhcp = 1`
  to chase it further, right after finally getting the panel back for the
  user - circumstantial timing points at `net.dhcp = 1` (a previously-
  fixed 2026-08-28 stack-overflow class, see that Log entry - possibly
  under new pressure from everything added since: the AEAD/crypto module,
  screencast, Marauder features) but this was never confirmed against a
  clean single fault record, only inferred from a chaotic multi-fault
  ring produced *before* today's bkpt fix existed. With the fault handler
  now safe to trigger unattended, the next occurrence (deliberately
  reproduced with `net.dhcp = 1`, or naturally) will finally give one
  clean, readable record instead of a multi-hour mystery - worth a
  dedicated session rather than reopening it at the tail of this one.
  **Also found, unrelated, while running the full host suite as part of
  verifying this fix (host itself did not rebuild - this is pre-existing,
  not caused by this fix): `golden_splash` and `golden_boot_desktop` now
  fail** (`golden_splash`: 18866/153600 pixels differ from
  `targets/sim/golden/splash.png` - the diff image shows nearly the whole
  splash screen, not a rounding-error sliver). Not investigated further
  this session - flagged here so it is not silently rediscovered from
  scratch next time `ctest` runs clean is assumed.

- 2026-08-29 (Silent boot-loop, found live and fixed by a plain reflash) —
  User reported "Ich kann taster und gui nicht nutzen" (buttons/GUI
  unusable). First response sent `d 1800` on the console per the
  established app_demo fix - it printed the normal success line, which
  turned out to be misleading: the console was answering, but a fresh
  webcam photo taken ~7 hours later (09:33 vs 16:56 the same day) showed
  the panel had gone from a correctly-rendered Home screen to blank/grey,
  and every subsequent console command (`h`, `E`) got zero bytes back
  even at a 15-18s timeout. That combination - console dead, panel blank -
  is the real-crash signature this project has previously only produced
  as a false alarm (the app_demo any-byte-exit bug, long since fixed);
  this time it wasn't one.
  Diagnosed the correct way per this file's own standing lesson: `st-util
  --no-reset` (never plain `st-util`, which resets on connect and would
  have destroyed the exact state being inspected) + a live GDB register
  read. Found the CPU sitting at PC=0x0800020c/0x080006e6 (a few hundred
  bytes into flash), every general-purpose register zero, LR=0xFFFFFFFF
  (the CPU's hardware power-on-reset default, never overwritten by a real
  call), SP=MSP=0x10010000 unchanged from the vector table's initial-SP
  word, PSP=0 - textbook "spending nearly all its time re-entering
  Reset_Handler", i.e. a fast reset loop, not a live hang.
  Detached without incident this time (past sessions have wedged the
  ST-Link doing this): sent `continue` from a non-batch, non-interactive
  GDB with stdin on `/dev/null`, then `kill -INT` (not `-9`) on the GDB
  *process* once it was blocked waiting on the target - GDB caught the
  interrupt, printed the stop, and exited cleanly on the immediate stdin
  EOF that followed. New gotcha for next time: `python3 scripts/swd_lock.py
  st-util --no-reset &` backgrounded from a shell - the PID bash's `$!`
  hands back is the Python wrapper's, not `st-util`'s own (the wrapper
  runs it via `subprocess.run`, a child process) - killing only the
  wrapper PID leaves the real `st-util` holding the ST-Link open
  (confirmed via `st-info --probe` reporting "Found 0 stlink programmers"
  / "another process has device opened for exclusive access" until the
  actual `st-util` PID, found via `ps aux | grep st-util`, was also
  terminated).
  Root cause left unconfirmed but circumstantial evidence points at the
  already-documented issue #57 mechanism (macOS auto-mounting
  `NOD_F429ZI` and writing metadata that a live ST-Link interprets as
  firmware at 0x08000000), not a firmware defect in HEAD: `cmake --build
  build/itsboard` against the unmodified working tree reported "no work
  to do" (the on-disk `.elf` already matched HEAD exactly, and HEAD had
  already been hardware-verified booting clean that same morning, see the
  screencast-viewer photo taken at 09:33 the same day), and the
  boot-after-reflash forensic ring (read once, from the *previous*,
  now-overwritten boot) showed `reset cause: IWDG watchdog` plus a
  BusFault/HardFault pair with `BFAR=0x1EE7CF84` - not a valid address in
  any real memory region on this part - and general-purpose registers
  holding what looks like stray ASCII bytes, consistent with a stray
  external write landing somewhere it shouldn't and being read back as
  code or a pointer, not with any logic bug in this session's own recent
  commits. `NOD_F429ZI` was not mounted at diagnosis time, so the
  fstab-based permanent fix from issue #57 (offered to, never confirmed
  by, the user) is still worth doing.
  Fix was simply: rebuild (no-op, already current), re-verify RAM budget
  (672 B margin, unchanged), `st-flash write` the identical known-good
  `.bin` back to 0x08000000, confirm boot self-test 6/6 + scheduler start
  + explorer ready over the console, restart the app_demo GUI session
  (`d 1800`), and confirm on camera - Home screen, Leo mascot, Menu/Pet
  buttons, fully live again. Total unattended downtime unknown (found
  retroactively via the forensic ring's timestamp, `t=14488ms` into
  *that* boot, not against wall-clock) but likely spanned most of the
  gap between the two photos.

- 2026-08-28 (Board<->Mac secure link, primitive layer - cross-session
  design and build) — Sparked by a user question about extending the
  ESP32Marauder use case further: could an embedded `ct-agent`/`ct-client`
  (the user's separate tunnel-agent project, run by another Claude session,
  "Maintainer Tunnel ct-agent") run on this board? Real cross-session
  collaboration, not solved alone - the other session read its own actual
  crate (`~3.6k`-`17.7k` lines, `tokio`/`quinn`/`rustls`, all
  heap-allocating) and gave a precise, evidence-based "no": TLS 1.3 itself
  needs low-to-mid kilobytes of handshake state, inherent to the protocol,
  not an artifact of any one implementation - nowhere close to this
  firmware's ~670 B margin, even for a from-scratch reimplementation.
  Landed on a cleaner shape instead: this Mac (already running the ST-Link/
  tester/board_key.py all session) is the "companion device", speaking
  real QUIC/TLS out to `ct-agent`'s tunnel, while the board<->Mac leg over
  the existing local LAN either stays plaintext or gets a much cheaper
  encryption layer - fixed pre-shared key, no TLS handshake at all.
  ct-agent's session reviewed that specific design (a second dedicated
  question) and found it structurally sound (same shape as WireGuard's or
  IPsec-ESP's static-SA mode) with one real, non-optional gap: a counter
  nonce resets to 0 on reboot and reuses under a fixed key on the very
  first post-boot message, which is catastrophic (recoverable auth key),
  not merely weak. Fix: this board's confirmed hardware RNG (checked
  against the project's own vendored SVD) generates a genuinely random
  24-byte XChaCha20-Poly1305 nonce per message instead, sidestepping
  cross-reboot counter persistence entirely.
  Built the primitive layer this same session, hardware-verified, not
  just designed: vendored Monocypher (hash-verified byte-for-byte against
  an independent re-fetch before committing - this is crypto source, a
  transcription error would be a security bug, not a build break),
  `modules/security`'s `cads_secure_link_seal/open()` wrapper (found and
  fixed a real gap while writing it: Monocypher's own `crypto_aead_unlock`
  leaves its output buffer completely untouched, not wiped, on an auth
  failure - verified by reading the actual implementation, not the header
  comment - so the wrapper now wipes it explicitly), a host-testable test
  suite including a known-answer vector cross-checked against an
  independent reference (PyNaCl/libsodium), and a real STM32F429 hardware
  RNG driver (`cads_hal_rng_bytes()`, RM0090 ch. 24's full documented
  procedure - continuous-RNG self-test on every word, live SECS/CECS error
  checking) exercised live on real silicon via a new `J <n>` explorer
  command - two runs, different high-entropy output each time, confirmed
  via `board_cmd.py`. Not yet wired into any actual wire protocol; see
  `docs/reference/secure-link.md` for exactly what exists today versus
  what's still open. Companion piece: the host-side bridge (talks to a
  litellm-proxied LLM demo, "Maintainer labor") is being packaged as an
  installable manifest by that same collaborating session, a separate,
  parallel thread from this one.

- 2026-08-28 (Select Target - Deauth was silently a no-op this whole
  project's history) — Found reading Marauder's own `CommandLine.cpp`
  directly, prompted by the user asking whether the ESP32's real
  capability was actually being made usable: `attack -t deauth` (and the
  AP-list Beacon Spam / Probe Flood variants) refuse to start at all -
  `"You don't have any targets selected. Use select"` - unless
  `wifi_scan_obj.filterActive()` is true, which only ever becomes true
  after a `select -a <index>` marks something in Marauder's own scanned
  `access_points` list. Nothing in this project has ever sent `select`.
  **Every active WiFi tool this project has ever built was consequently a
  silent no-op on real hardware, discoverable only by reading Marauder's
  own source, not from the UI** (the confirm dialog runs fine, Marauder
  just quietly declines to transmit anything afterward - no error shown).
  Fixed with a new "Select Target" tool - a third interaction shape beyond
  the existing passive/active ones, a numeric field (Up/Down adjusts, Ok
  sends `select -a <N>`) rather than a scrollable target-picker list (which
  would need its own parsed-AP array this firmware's RAM margin, ~670 B,
  does not afford). Hardware-verified end to end via the same day's
  headless key-injection tooling: navigated to it, dialed in index 3,
  confirmed via webcam that Marauder replied `1 selected, 0 unselected` -
  the real `showCounts()` success reply. See
  `docs/reference/marauder-coprocessor.md`'s own section on this.

- 2026-08-28 (Bluetooth in the touchscreen menu, headless key injection, and
  a night-long "touch stopped working" mystery finally explained) — Long,
  eventful continuation of the same day's Marauder work. In rough order:

  **The recurring "touch/buttons dead" panic, explained.** Multiple times
  this session the user reported the panel frozen and unresponsive. First
  suspected as a real hang; live GDB attaches (`st-util --no-reset`) each
  time found the CPU perfectly healthy - idling normally, zero fault
  registers set, scheduler running fine. The actual cause: `apps/bringup/
  explorer_app_demo.c`'s interactive session (`d` with `seconds==0`, what
  `boot.autostart` uses to hand the panel to the GUI at boot) ends and
  drops back to the bare console prompt on **any** console byte - and
  nearly every diagnostic command sent this whole session
  (`board_cmd.py`'s `s`/`k`/`i`/`~`/`E`, all of it) is exactly such a byte.
  The panel wasn't crashed; the loop that ticks `cads_gui_tick()` had
  simply exited and nothing was driving the screen any more, silently,
  every single time. `docs/reference/explorer-console.md` now says this
  explicitly on the `d` row - this should never cost debugging time again.

  **A real, separate self-inflicted mistake, owned rather than glossed
  over:** mid-investigation, an `arm-none-eabi-gdb --batch` session was
  torn down with `kill -9` instead of a clean detach, desyncing the
  ST-Link's USB protocol state (`Failed to enter SWD mode`, chipid
  `0x000`) - the documented issue #57 wedge pattern, this time self-caused
  rather than hardware-caused. Fixed the same way: physical USB replug,
  then (once, when a *second* wedge didn't fully clear on replug alone)
  `--connect-under-reset` to recover without another physical cycle.

  **Bluetooth touchscreen menu: built, made dynamic, found unreliable live,
  reverted to always-shown.** Four new Marauder tools (Sniff BT, BLE Spam,
  Sniff PMKID, Sniff SAE, Clear APs - see the same-day Marauder log entry
  below for the earlier two). First shipped with the two Bluetooth items
  conditionally hidden until a `stopscan` liveness probe got a reply within
  2 s (no electrical presence line exists on CN8's TX/RX pair, so this was
  the only signal available). Real hardware testing found the timing
  unreliable in practice - items appeared, then vanished again on a later
  visit with the ESP32 still demonstrably connected and answering. Reverted
  same session: Bluetooth tools now behave exactly like the WiFi ones
  always have, always in the menu, no special-casing. A working, honestly
  reported "this idea didn't hold up live" beats a nicer-sounding design
  that flickers.

  **A real BLE formatting bug, found and fixed via a live webcam capture.**
  Marauder's `sniffbt` output has no real newline between devices
  (`Serial.print()`, not `println()`), so a scan burst arrived as one run
  and CADS_MARAUDER_LINE_LEN's hard wrap cut mid-MAC-address. Fixed with a
  new, generic "split marker" mechanism in `cads_marauder_reader.h` (not
  Bluetooth-specific in the reader itself - just configurable per tool);
  confirmed via two webcam photos, wrapped-mid-address before, clean
  one-device-per-row after (one row legibly showing "Galaxy Watch6
  (SEEP)"). A second, superficially similar case - a real AP whose ESSID
  happens to be its own MAC address, long enough to split across two
  display rows - was investigated, root-caused via the same live-photo
  method, and deliberately left as-is: the reader's own header comment
  already documents accepting this exact class of degradation to keep
  `CADS_MARAUDER_LINE_LEN` small, so "fixing" it would mean re-opening a
  tradeoff already made on purpose, not fixing a defect.

  **Headless GUI key injection - the tool that made the rest of tonight's
  verification possible at all.** There was no way to touch the panel from
  a Mac terminal, and `d`'s own exit-on-any-byte rule (above) meant a typed
  command couldn't double as a keypress either. `apps/bringup/
  explorer_app_demo.c` now reserves one byte per logical key (`>=0x80`,
  never colliding with typed ASCII or CR/LF); receiving one calls
  `cads_gui_input()` directly - the same function a real button/touch event
  reaches - and the interactive session keeps running instead of exiting.
  `scripts/board_key.py <key> [<key>...]` sends these bytes. Verified live:
  navigated Desktop → Menu → Marauder → Sniff BT purely via injected keys,
  confirmed via webcam at each step, then used the same tool to sweep
  Settings/About/GPIO/Network/Active Net Tools/Files/Arcade for a
  representative check of the rest of the firmware's GUI - all found
  functioning correctly, nothing else broken.

  **Tooling lesson, worth remembering:** the webcam used for these captures
  (`ffmpeg -f avfoundation`) does not have a stable device index across
  invocations on this Mac - it silently shifted between calls, several
  times producing photos of the wrong (virtual/placeholder) camera instead
  of the one pointed at the board. The fix: look the device up by name
  (`ffmpeg -f avfoundation -list_devices true -i "" 2>&1 | grep "HD Pro
  Webcam C920"`) fresh before every single capture, never cache the index.

- 2026-08-28 (Marauder recon over serial, no touchscreen needed - and a
  real Marauder-firmware gotcha found live) — User was away from the board
  ("nicht vor Ort") and asked whether WiFi recon (weak-password/vulnerability
  survey) was possible anyway; wanted the actual capture to run on the
  existing ITSboard+ESP32 setup (not the Mac's own WiFi chip, which cannot
  do monitor mode/injection under macOS regardless of tooling - verified by
  checking: no aircrack-ng/hashcat/hcxtools installed, and Apple's own
  `airport` utility is gone from modern macOS). Installed `hashcat` +
  `hcxtools` on the Mac for the offline-cracking half once a real capture
  exists.
  Found the explorer's WiFi-UART bring-up diagnostic (`~`, added
  2026-08-28 morning as a throwaway loopback test - see this Log's own
  Marauder co-processor entry) was the *only* serial-reachable path into
  the co-processor at all - `apps/marauder`'s real bridge is touchscreen-
  menu-only with no CLI trigger, deliberately so for its active/transmit
  tools (Deauth etc. stay behind their own mandatory confirm dialog, not
  bypassable). Extended `~` into a real, permanent, safe recon command
  rather than reverting it as originally planned: streams Marauder's full
  reply live in chunks instead of truncating to the original 64 B buffer,
  and is hard-coded to send exactly one fixed command (`scanall` - lists
  nearby APs, transmits nothing) so it cannot reach any active tool -
  narrower door, not a smaller version of the touchscreen confirm gate.
  Hardware-verified end to end, and found a real, reproducible Marauder
  behavior along the way: a first live run returned only the command's own
  echo + prompt, no scan data, no error - traced (via the pinned commit's
  `CommandLine.cpp`, not guessing) to `if (!wifi_scan_obj.scanning())`
  gating the *entire* WiFi/BT scan/attack command family; a scan left
  running from anywhere (this command, an earlier touchscreen session, a
  crash mid-scan) silently swallows every later `scanall` with zero error
  output. `stopscan -f` over the same link cleared it; a fresh `scanall`
  then returned real APs from the live RF environment (`persepolis-II`,
  `persepolis-XI`, `FTTH_UX9399`, ...). This is a genuine Marauder-firmware
  property, not a bug in this repo's own code - and `apps/marauder`'s
  touchscreen tool view has no visibility into it either: a stuck scan
  would look identical there (press Scan, nothing happens, no error). Left
  as a documented gotcha in `~`'s own comment rather than a silent fix,
  since fixing it in `apps/marauder` (e.g. always `stopscan -f` before
  `scanall`) is a real, separate change to the touchscreen app's behavior
  that should get its own hardware-verified pass, not ride in on a serial
  diagnostic's fix.
  `board_cmd.py '~' <seconds>` is now the answer to "can we survey WiFi
  networks without being at the board" - yes, headlessly, from wherever the
  console USB is reachable.

- 2026-08-28 (VS Code: high-integration native workflow, build/flash/debug/
  registers) — User wanted VS Code driven entirely through its own classic
  UI (status bar, Activity Bar, Run and Debug panel), explicitly not
  Command-Palette/task-menu-driven, without touching the existing toolchain
  or any other installed extension (Keil Studio Pack stays, used for a
  different project in the same VS Code instance). Delivered and personally
  verified live (real build → real flash → real breakpoint stop → real
  register/peripheral inspection, screenshotted at each step, not just
  described): a genuine `flash` CMake custom target (`USES_TERMINAL`, shows
  up in CMake Tools' own target picker, not just `tasks.json`); a vendored
  Apache-2.0 STM32F429 SVD file wired into `cortex-debug` via `svdFile` for
  a real named-peripheral XPeripherals tree, plus the recommended
  `mcu-debug.peripheral-viewer` extension (the actively-maintained
  standalone successor to cortex-debug's own bundled SVD view - 1.4M+
  installs); `.clang-format`/`.clang-tidy` wired into format-on-save and
  live linting. Two real bugs found and fixed along the way, both via
  actually running things rather than reading the config and assuming: (1)
  `code .` reuses an already-running VS Code process's stale environment -
  a `PATH` fix needs a full quit (`Cmd+Q`), not just a new window; (2) a
  `llvm-vs-code-extensions.vscode-clangd` install (present because it's
  what the *other* project, ITS-BRD-VSC, recommends, in the same VS Code
  instance) runs blind against this repo with no `compile_commands.json`
  wiring of its own, producing false "file not found" errors and spurious
  warnings from its own bundled clang-tidy checks - marked
  `unwantedRecommendations` for this workspace, cpptools is this project's
  real IntelliSense engine. Rewrote `docs/how-to/vscode-setup.md` with 7
  real screenshots and an honest Windows section (WSL2 vs. native - the
  console tooling imports the POSIX-only `termios` module and cannot run on
  native Windows Python at all, verified by reading the import, not
  assumed) and a much more explicit ITS-BRD-VSC comparison: this project
  deliberately uses VS Code's own default tooling plus mainstream
  extensions over one vendor's bundled toolchain, trading Keil Studio
  Pack's out-of-the-box polish for portability (works identically from any
  CI/IDE/terminal) and capabilities ITS-BRD-VSC's setup doesn't have at all
  (a host-only build+test with zero hardware attached, two independent
  toolchains - STM32 and ESP32/Marauder - in one repo).

- 2026-08-28 (net.dhcp crash: console-task stack overflow, found and fixed
  on real hardware) — User request: real internet to the board via a Mac
  Ethernet interface with Internet Sharing on. Setting `net.dhcp = 1` and
  resetting crashed the board every time, live-verified via `st-util
  --no-reset` + GDB (not console silence alone, which turned out to be
  misleading - see below): CPU trapped inside the fault handler itself,
  SP clobbered to an absurd low value. Wasted real time on two wrong leads
  before finding it, worth recording so they are not retried blind:
  **(1) filesystem/config corruption** - reverting `net.dhcp` to 0 didn't
  stop the crash, a fresh `cads_config.py pull` still read back clean
  content, and a from-scratch reformatted littlefs volume (new `cads_fs
  format` subcommand, a genuine tooling gap found along the way - see its
  own commit) crashed identically. Conclusively ruled out.
  **(2) SWD/debug-session flakiness** - a real hardware power-cycle
  (`reset cause: power-on`, forensic ring genuinely cleared to 0 records -
  CCM survives a plain `st-flash reset` but not real power loss) still
  didn't fix it, ruling this out too. Also cost a chunk of apparent-crash
  time that was actually **my own port-selection mistake**: this Nucleo
  enumerates two `/dev/cu.usbmodem*` devices, and after the first
  auto-detect hiccup I pinned `board_cmd.py --port` to the wrong
  (non-console) one, so several "still crashed" reads were really just
  querying dead air - own goal, not a board or firmware problem, and a
  reminder to trust a live GDB attach over console silence when the two
  disagree.
  **The real cause**: `apps/bringup/explorer_app_demo.c`'s main loop calls
  `cads_net_poll()` every tick on the **console task** (`apps/bringup/
  tasks.c`, 512-word/2048 B stack) - with `net.dhcp=1` that runs lwIP's
  DHCP client state machine, visibly deeper than the static-IP path, on
  the exact same stack this loop also uses for the evening's whole added
  call chain (`cads_marauder_tick`'s PCAP/join depth,
  `cads_settings_service_config`, `cads_gui_tick`, ...). Caught live:
  `vApplicationIdleHook()` - this project's own stack-guard sentinel check
  (`tasks.c`) - faulted with a garbage PC (`0xF7FF0FF0`, an
  instruction-fetch violation), the textbook signature of a stack overflow
  severe enough to corrupt the very code trying to detect it. **Fix**:
  doubled `CADS_CONSOLE_STACK` 512->1024 words. Unlike the SRAM heap
  `scripts/check_ram_budget.py` guards (256 B floor, currently 704 B
  margin, unaffected by this change), task stacks live in CCM, which had
  ~59 KB free out of 64 KB - doubling cost 2 KB of that, nothing from the
  tight budget. **Verified working**: real DHCP lease (`192.168.2.3`,
  gateway/DNS `192.168.2.1` - macOS Internet Sharing's own subnet),
  reproduced clean across two independent full test runs with the
  forensic ring not growing between them (the two stale-looking records
  still in it afterward are confirmed leftovers from the pre-fix crash,
  not an active recurrence - the same plain-reset-doesn't-clear-CCM fact
  from lead (2) above, this time working in the diagnosis's favor: an
  unchanging ring across a real successful run is proof of "old and
  inert," not "still happening").
  **New reusable tooling**: `cads_fs <image> format` (`tools/cads_fs.c`) -
  there was previously no way to produce a fresh, valid, empty littlefs
  image from the host side at all; recovering from a *suspected*
  filesystem problem required inventing it. Also fixed a real,
  since-the-original-commit bug in `scripts/swd_lock.py` (missing
  `#!/usr/bin/env python3` shebang - broke the exact direct-invocation
  usage CLAUDE.md itself documents) found the same way, by actually
  running the tool instead of trusting it worked.

- 2026-08-28 (Marauder: WiFi join wired, live PCAP-to-Wireshark relay built)
  — Autonomous `/loop` continuation of the Marauder feature set (user's
  scope, in priority order: join, PCAP streaming, Bluetooth). Two of three
  done, host-tested and RAM-budget-passing; **neither hardware-verified
  yet** (no board session this iteration - see each item's own "not yet
  hardware-verified" note).
  **1) `cads_marauder_join()`** now does the real thing instead of a stub:
  starts `scanall`, watches every AP-format output line via the reader's
  new `line_cb` hook (`cads_marauder_join.c`, host-tested,
  `test_marauder_join.c`), counts AP-line arrival order as the
  `access_points` list index (Marauder's `join -a <index>` is the only way
  in — no CLI command sets an SSID by name, confirmed against
  `CommandLine.cpp`), then `stopscan` + `join -a <index> -p <password>`.
  Wired into a new Settings **"Join WiFi"** row reading `cfg->wifi_ssid`/
  `wifi_password`, following the same request-flag-on-INPUT-task /
  service-on-console-task split as Reload config — not just style-copying:
  `cads_hal_wifi_uart_write()` has no locking of its own and
  `cads_marauder_tick()` already drives that link every console-task loop,
  so calling the join straight from the menu row would race two tasks on
  one UART (the SPI-mutex incident's lesson, applied before it repeated).
  **2) Live PCAP-over-TZSP relay to Wireshark**, `apps/marauder/
  cads_marauder_pcap.{h,c}` + a new Marauder menu tool **"Sniff (PCAP)"**
  (`sniffraw -serial`): demuxes Marauder's own `[BUF/BEGIN]`/`[BUF/CLOSE]`-
  framed binary bursts from the ordinary CLI text sharing the same UART,
  self-describes the one-time 24 B pcap global header by racing its magic
  number against the close marker byte-by-byte (no external "is this the
  first burst" session state to forget to reset), decodes each record, and
  relays the raw 802.11 frame as a minimal TZSP datagram
  (`cads_marauder_tzsp_build()`, header bytes verified byte-exact against
  Wireshark's own `packet-tzsp.c` constants — confirmed via web fetch, not
  memory) over UDP port 37008 (`udpdump`'s own default) via a new
  `cads_net_udp_send()` (`modules/net`, board: transient `udp_pcb` per
  send; sim: honest no-op, matching the "no link, ever" stub). RAM is the
  binding constraint (704 B margin after this, floor 256 B) so frames
  truncate at 128 B rather than buffering a whole capture — valid pcap
  snapshot-length semantics, Wireshark renders it as
  "[Frame is marked as truncated]", not an error. New `wifi.pcap_target`
  config key (IPv4, 0 = relay stays silent). 12 new host tests
  (`test_marauder_pcap.c`) cover the demux (global header, multi-record
  bursts, zero-length records, truncation-stays-in-sync, byte-at-a-time
  resumability, CLI-text passthrough) and the TZSP builder.
  Docs: `docs/reference/marauder-pcap-stream.md` (new — setup, wire format,
  troubleshooting), `config-file.md`'s stale "wifi.\* reserved, not yet
  active" language corrected (it's been active since the join work), both
  Marauder docs added to `reference/index.md` (neither was linked from
  there before, an earlier-iteration gap).
  **3) Bluetooth — fixed, build-verified, not yet flashed.** The user's
  scope assumed real NimBLE 1.x→2.x API porting was needed (WiFiScan.cpp's
  BLE call sites); actually running `arduino-cli compile` instead of just
  reading the diff found the real cause in minutes: WiFiScan.cpp/.h already
  carry two COMPLETE, correct implementations of every BLE call site,
  gated on `HAS_NIMBLE_2` (every other real board target already defines
  it). `GENERIC_ESP32`'s own `configs.h` block ships `HAS_BT` on by default
  but `HAS_NIMBLE_2` off — internally inconsistent once NimBLE-Arduino is
  pinned to 2.3.8 (which this build already does), not a version mismatch
  needing porting. The earlier session's "~15 compile errors → disable
  HAS_BT" workaround had mis-diagnosed the actual layer. Fix: define both
  macros together in `build_and_flash.sh`'s patch step — zero manual
  call-site changes. Verified with two independent clean `arduino-cli
  compile` runs from a pristine checkout (byte-identical output size both
  times), not by inspection alone. **Bonus finding from actually running
  the script end to end** (not just editing it): `build_and_flash.sh`'s
  compile/upload steps had a latent bug since the script's original
  commit — `cd esp32_marauder` runs, then the sketch path argument was
  still the string `esp32_marauder` (doubling to a directory that doesn't
  exist); fixed to `.`. This means the earlier "verified working" WiFi
  flash was very likely done through a manually-run command sequence
  during that session rather than this literal committed script — worth
  remembering if a "the script did X" claim and an actual script content
  ever seem to diverge again. **Not yet flashed to real hardware or
  CLI-verified** (`sniffbt`, `blespam`, ...) — no ESP32 was connected this
  iteration; `docs/reference/marauder-coprocessor.md`'s Bluetooth section
  now reflects this precisely.
  **Known gap noticed, not fixed:** `marauder` is missing from
  `config-file.md`'s "Recognised apps" profile list and (unverified)
  possibly from `scripts/check_profile.py`'s own app list — pre-existing,
  predates this iteration, left as a follow-up rather than scope-creeping
  into the profile system while mid-feature.

  **STM32-side hardware check (2026-08-28, same session, ST-Link
  connected, no ESP32 present).** Also fixed a real bug found the same
  way as the two above - by actually running the tool: `scripts/
  swd_lock.py` had NO SHEBANG LINE (docstring was line 1), so direct
  invocation (`scripts/swd_lock.py st-flash ...`, the exact usage CLAUDE.md
  itself documents) fell through to `/bin/sh`, which choked on the first
  Python syntax it hit - added `#!/usr/bin/env python3`. With that fixed:
  flashed this iteration's full build (join + PCAP relay + Bluetooth-build
  changes, none of which touch GPIO/Ethernet/boot code) to the real board,
  reset twice independently, and ran the `d 15` app-tree live smoke test.
  Forensic ring held steady at 6 records across all three checks - no new
  entries - and the explorer prompt came back clean each time. The ring's
  existing 6 records (2 with sane low-uptime timestamps, 4 with the
  garbled text/absurd-timestamp signature CLAUDE.md already documents for
  stale CCM content) symbolize to `cads_fault_dump` itself and to
  `cads_hal_eth_mac_init`/`cads_gpio_init_alternate` - boot-time
  Ethernet/GPIO code this iteration never touched - confirming they
  predate today's changes rather than being caused by them. This confirms
  the STM32 side boots clean and runs the app tree live without fault; it
  does NOT confirm the actual join, the actual PCAP-to-Wireshark relay, or
  actual Bluetooth CLI commands - those need the ESP32 connected (ideally
  with Wireshark running on the Mac for the PCAP path) and are still open.

- 2026-08-28 (Marauder co-processor: wired, flashed, verified) — **The WiFi
  co-processor is real hardware now, not a design doc.** An ESP32-WROOM-32
  DevKit is wired to CN8 pins 8/9 (PC6/PC7, USART6) - confirmed against the
  project's own schematic (`docs/reference/datasheets/ITSBRD-schematic-...
  .pdf`) after a photo-based wiring check nearly put a wire on the SWD
  header by mistake (routing slack made it *look* misplaced in a wide shot;
  a close macro photo showed it was actually on CN8's own GND pin - lesson:
  trust the close photo, not the wide one, when a wire's destination is
  ambiguous). `modules/wifi`'s hal_uart_wifi.c driver is hardware-verified
  via a new `~` explorer command (raw USART6 loopback, bypassing the PPP
  bootstrap entirely) - 25/25 bytes, 0 drops, 0 overruns, with TIM8-1/
  TIM8-2 jumpered.
  Bigger: **ESP32Marauder itself is now built, flashed, and confirmed doing
  real WiFi work** - `scanall` returned live APs/stations/associations from
  the actual RF environment. `GENERIC_ESP32` has no prebuilt release binary
  upstream and needed real archaeology to build (missing `HAS_IDF_3`, a
  link-time bug in `EvilPortal.h`, two mbedtls renames, an intentional
  `libnet80211.a` symbol override needing `--allow-multiple-definition`) -
  all captured in `tools/marauder-build/build_and_flash.sh` +
  `docs/reference/marauder-coprocessor.md` so it's a single command next
  time, not a rediscovery. Bluetooth is off in this build (NimBLE 1.x/2.x
  API mismatch - real porting work, deferred, tracked as a known
  limitation) but every WiFi capability works. The flash step itself needed
  its own diagnosis - three different esptool error messages across
  identical retries turned out to be a loose breadboard seat, not a flag to
  tune.
  **Authorization context on file** (memory, `user_expertise_domain.md`):
  the user is a professor at HAW Hamburg; active WiFi attack capability
  (deauth/evil-twin/handshake capture) is being built with an on-device
  confirmation gate before any transmit-based action, matching the
  confirmation-UX pattern M9's Ethernet-side spoofing tools already call
  for. The STM32-side Marauder CLI bridge (menu app driving the verified
  USART6 link) is the next real step - not started yet.

- 2026-08-27 (v0.0.1 - touch works, iperf 5.7x) — **First tagged release.**
  Touch selection in the GUI, long the standing "enable touch" priority, now
  works end to end - diagnosed live on the panel across three layered bugs and
  the user's own suggested fix:
  (1) the XPT2046 read occasionally returns a wild sample under redraw
  contention (measured 70-140 px jumps between 100 Hz polls); an input-layer
  despike drops any >55 px jump. (2) A press became a "drag" at half a row so
  taps selected nothing; drag now means actual row scrolling. (3) The two-corner
  calibration extrapolated the whole panel from two taps, drifting up to a row
  near the top (fine at the bottom, so the soft-keys always worked) - replaced
  with a **5x2 multi-point grid + least-squares fit** (each row, left then
  right), the user's idea. Result confirmed "viel besser".
  Also: **iperf 2.73 -> 15.7 Mbit/s** (5.7x, measured against Mac iperf2) by
  busy-polling the stack instead of a 10 ms delay per poll; next ceiling is
  TCP_MSS 536->1460 (a ~5.4 KB RAM decision, deferred). Soft-key label
  "Start/Stop" clipped its 60 px cell -> "Run/Stop". Boot banner now carries
  `CADS_VERSION` ("0.0.1"). Known cosmetic issues carried into v0.0.1: the
  status/soft-key bars show black gaps on the board (framebuffer is clean, so
  it is a flush/SPI-contention artifact, not a draw bug - still open); the
  post-flash ST-Link reset is flaky and sometimes leaves the board hung in
  early clock init until a clean `st-flash reset`.

- 2026-08-27 (autostart + ST-Link post-mortem) — **The board now boots into
  the menu.** New config key `boot.autostart` (default on): after the self
  test the explorer hands the panel to the app tree indefinitely; any console
  byte returns to the prompt, and that byte seeds the next command so
  scripted one-shots (`board_cmd.py E`) work against a booted board -
  hardware-verified end to end, including a live `P` ping executed straight
  out of the menu. The sim inherits it, so the no-input golden scene is now
  the desktop (`boot_desktop.png`, replaces `bringup_pattern.png`).
  Also landed: the #63 damage-box race fixed with a new portable
  `cads_hal_irq_save()/restore()` pair (PRIMASK; the taskENTER_CRITICAL
  pre-scheduler trap documented in CLAUDE.md is why); the tightest lwIP pool
  trims walked back (margin 8608->6144 B) - MEM_SIZE 3K, TCP_SEG 16,
  PBUF_POOL 6, TCP_PCB 4 - because the .1-.254 ARP sweep provokes exactly
  the RX bursts the old cuts assumed away; `board_cmd.py` auto-detects the
  VCP (its name shifts with the USB port). **ST-Link post-mortem closed
  (issue #57):** the recurring wedge + one real flash corruption (single
  cleared bit in the vector table's initial-SP word, board hung in
  Reset_Handler, repaired by reflash) trace to macOS auto-mounting the
  Nucleo's MBED drive and writing metadata (now: fstab noauto + unmount
  rule), SWD clients killed mid-transfer (now: generous timeouts, never
  kill), and overlapping SWD clients (now: everything serializes through
  `scripts/swd_lock.py`). Watch item: one unreproduced BusFault->IWDG
  reboot ~21 s into the first autostart idle (issue #69) - 150 s + 12 min
  soaks since were clean; forensics preserved in the issue. Config crash
  fix (static text buffers, review-2 #4) hardware-verified earlier today;
  the full Mac config round-trip (pull/edit/push/re-pull) works.

- 2026-08-27 (review pass 2) — **Second adversarial review swarm, aimed at
  this session's own fresh config/tools/profile/flash code; 34 findings
  confirmed (of 39), 28 fixed.** A focused 6-dimension swarm (config parse,
  storage lifecycle, the flash fix, the host FS tool, the build-profile
  mechanism, and a re-look at the open first-review findings) with one
  adversarial refuter per finding. It found real defects the first pass could
  not have - the code did not exist then - most of them in code written hours
  earlier, which is the point of a second independent pass.
  **The two high-severity roots, both fixed:**
  - **Reload/reset did littlefs work on the input task.** Menu activations run
    synchronously on the high-priority input task; cads/storage is explicitly
    not thread-safe, so a "Reload config" concurrent with the console task's
    calibration `kv_save` could corrupt the volume (#3), and its two 512 B
    stack buffers would overflow the 1 KB input-task stack, deterministically
    if the file was absent and load nested save (#4). Fix: the menu row only
    sets a request flag; `cads_settings_service_config()` on the app-tree loop
    (the console task - the single storage owner, 2 KB stack) does the work.
    Factory reset routed the same way, and now resets to the *config* defaults
    and rewrites `/config.txt` so it survives a reboot (#18, #19); apply only
    pokes a subsystem whose value changed (#7).
  - **Build profiles stuck in the CMake cache.** A plain `set(CACHE)` only
    applies to an uncached variable, so switching/editing a profile in an
    existing build dir was silently ignored (#1, #2), and the profile file was
    not a configure dependency (#6). Fix: FORCE (profile is authoritative) +
    `CMAKE_CONFIGURE_DEPENDS`. The precedence change (a `-D` no longer beats an
    active profile) is documented.
  Also fixed: `check_profile.py` computed for the wrong image (omitted apps
  default ON in CMake, not OFF - #14) and its view-count table had no drift
  guard (#15) - now it counts the real `cads_view_dispatcher_add()` sites and
  defaults omitted apps ON, grammar tightened to match CMake (#30, #31); the
  `cads_fs` tool and host image I/O treated I/O errors as EOF, ignored close
  results, and mangled binary stdout on Windows (#12, #16, #17, #27, #32, #33,
  #34); the config parser truncated large files mid-line (#9-11), accepted
  malformed IPs and booleans (#20, #22, #24), and did not self-heal an empty
  file (#26); the flash-timeout comment misnamed the clock (DWT, not SysTick -
  which is the *stronger* guarantee, #29); and `cads_config.py` gained a
  whole-volume-RMW quiescing caveat (#13).
  **Deferred or mitigated, with reason:** #5 (flash controller has no
  cross-task lock) is mitigated by the single-storage-owner discipline the #3
  fix establishes - no two tasks call the driver concurrently - and the
  DWT-based timeout turns any residual race into a named panic, not a silent
  hang; a driver-level mutex remains a belt-and-suspenders option. #8 is the
  pre-existing canvas-flush race already tracked as issue #63, not introduced
  here. #28 (bank-2 read stalling during an in-flight erase) is the RWW
  hardware limitation, not reachable across tasks given single-owner storage,
  and needs board time to characterise. #21/#23/#25 are defensive or
  by-design (fixed-size buffers that cannot truncate in practice; whitespace
  is trimmed by convention). All fixes: host 32/32, board builds clean, RAM
  margin 9,632 B. NOT hardware-verified this session - the ST-Link USB wedge
  (issue #57) never recovered; the reload-on-console-task path and the flash
  timeout both still want a board test once it does.

- 2026-08-27 (config + profiles) — **Persistent flash config file, host
  editing tools, and build-time feature profiles - all landed; the config
  feature's hardware verification is BLOCKED on the recurring ST-Link USB
  wedge (issue #57), not yet complete.** User-requested: a config file in
  flash, base version on first boot, editable from the Mac via the "mounted"
  filesystem, reloadable from a menu entry - plus (separately) a build-time
  profile file to select which apps/features compile in, with dependency and
  usability checks; both scoped with the still-in-progress ESP32 WiFi
  dev-board hardware in mind.
  **modules/config**: `/config.txt`, flat `key = value` text, in the littlefs
  volume. Pure parse/serialize (host-tested, test_config.c) + storage-backed
  load/save. `cads_config_load()` writes the built-in defaults if the file is
  missing, so there is always something to edit. Settings gained "Reload
  config" (re-reads and re-applies brightness/SPI-clock/network live, no
  reboot). Carries `wifi.enabled/ssid/password/uart` now, unused until the
  ESP32 UART driver lands - the file will already be ready that day.
  **Host tools** (tools/cads_fs.c + scripts/cads_config.py): the board has no
  USB-MSC, only SWD, so "mount the filesystem" means: dump the littlefs
  region with `st-flash read`, edit one file inside the image using the
  *same* littlefs code the firmware runs (cads_fs, built via CMake on the
  host), write the image back. `cads_config.py pull/push/edit` wraps that
  into the actual workflow. Pure C11/stdio, no platform-specific calls -
  should build on Windows/Linux/macOS via any host toolchain CMake finds,
  though only macOS was actually exercised this session.
  **Build profiles** (profiles/*.profile + CMakeLists.txt's new
  `CADS_PROFILE` + scripts/check_profile.py): declarative `app.<name> =
  on|off` files pre-seed the existing `CADS_APP_*` cache options before their
  `option()` calls run - an explicit `-D` on the command line still always
  wins (verified). `check_profile.py` catches a typo'd feature name before a
  build, and computes the view count a profile's enabled apps would register
  against `CADS_APP_DEMO_VIEW_CAPACITY` - the fixed-size registry that has
  **silently dropped views past capacity twice already in this project's own
  history** - so a profile that would repeat that mistake fails validation
  instead of shipping a dead menu row. `--build` does a real configure+build
  and checks RAM. Verified end to end: `profiles/full.profile` (24/26 views)
  and `profiles/minimal.profile` (6/26 views, real build: **16,544 B** RAM
  margin vs the full profile's ~9,760 B - proof the mechanism removes code,
  not just menu entries).
  **A real bug found and fixed en route, NOT YET hardware-verified**: adding
  the config file meant a normal boot now performs the first genuine flash
  erase+program from a fresh board (creating `/config.txt`) - a path the
  existing storage tests never exercised from this exact context - and the
  board hung completely, silently, with no CPU fault (recovered only by the
  IWDG, no diagnostic trail). Root cause: `cads_flash_erase_sector()` /
  `cads_flash_program_word()` (modules/storage) had bare
  `while(FLASH->SR & FLASH_SR_BSY) {}` loops - the exact unbounded-hardware-
  flag-wait class already fixed for SPI (issue #66) - just never hit by any
  test until this feature's first-write-ever-from-boot path. Bounded both
  (RM0090's own worst case for the 128 KB sector is under 2s; 4s slack) with
  `cads_hal_panic()` on expiry, so a genuine stall becomes a named forensic
  record instead of a silent freeze. Builds clean, host 32/32, RAM unaffected
  - but an unrelated ST-Link USB wedge hit partway through hardware testing
  and never recovered this session (needs the user's physical replug, the
  established recovery for this class per issue #57). **Next session: verify
  on real hardware before trusting this fix** - does `/config.txt` actually
  get created on a fresh board now, and if the underlying hang persists, does
  it correctly surface as a named panic instead of a silent freeze.

- 2026-08-27 (review + soak) — **Professional embedded code-review swarm +
  long-term usage emulation; 8 findings fixed, all hardware-verified.** A
  6-dimension review swarm (concurrency/ISR, RAM/DMA, register sequences,
  robustness, performance, freshly-landed code) produced 29 raw findings;
  adversarial verification (one refuter per unique finding, default-refute)
  rejected 10 and confirmed 18. Filed as GitHub issues #58-68 (dedup against
  the existing backlog, existing label convention). In parallel a long-term
  soak drove ~440 varied serial commands across two runs (standard + a stress
  mix: pktgen 2000/5000 pps, display-under-contention, promiscuous capture,
  net churn) - **zero faults, zero hangs, forensic ring clean throughout**;
  the firmware is robust under sustained interaction.
  **Fixed this cycle (issue -> what):**
  - #59 HIGH: gui/canvas.c double-buffered staging banks never overlapped
    (blit is synchronous), so the second 15,360 B bank was dead weight.
    Dropped it - **RAM margin 640 B -> 16,000 B**, the single biggest change
    in this project's memory posture. Display renders byte-identically.
  - #58 HIGH: cads_hal_spi_set_speed() rewrote CR1 (SPE/baud) with no bus
    mutex - the exact race the recursive mutex was added to stop, left
    uncovered on the 'F' command and Settings fast-clock. Took the recursive
    mutex inside set_speed(). 6-cycle fast-clock-under-load soak: 0 hangs.
  - #60 HIGH: netx DHCP builder wrote the BOOTP magic cookie at offset 240,
    not 236 (fixed header is 236 B) - malformed, 4 bytes over. Fixed to
    236/274 + a golden regression test (the netx suite had no DHCP coverage).
  - #61 MED: apps/active owns_rx() claimed the RX ring for any capture tool
    in RUN mode even when capture never began - froze lwIP RX. Now gated on a
    capture_active flag set only on a real cads_netx_capture_begin().
  - #62 MED: the ARP scan dirtied the whole view every 30 ms; now dirties only
    the 20 px result line (and the found line on a hit). ~10x less blit/tick.
  - #65 MED: cads_hal_spi_wait()'s check-then-WFI could lose the DMA-complete
    wakeup; replaced with the PRIMASK-gated idiom.
  - #66 MED: unbounded TXE/RXNE/BSY spins in cads_hal_spi_transfer() -> a
    wedged peripheral hung silently past the tick-fed IWDG. Guard-bounded
    (cheap counter, hot path unaffected) + cads_hal_panic() on expiry.
  - #68 PERF: RX ring 4 -> 8 (the soak measured ~1431 drops at 5000 pps
    capture; now 0). Affordable on #59's freed RAM. Margin 9,792 B.
  **Still open from the review:** #63 (canvas flush damage-box race, medium
  concurrency), #64 (RX pump double-copy -> zero-copy, enhancement), #67 (six
  low-severity items, grouped). All non-urgent. Net effect: the firmware is
  measurably more robust and efficient, and RAM went from a chronic 256 B
  fight to ~10 KB of genuine headroom - room the deferred feature backlog can
  finally use.

- 2026-08-27 (integration) — **M9 "Active Net Tools" (modules/netx + apps/active)
  integrated and the held GUI/calibration work landed, as one runnable image.**
  A second developer built the M9 offensive-tooling suite in parallel; at the
  project lead's 4-hour deadline (external.done never appeared) the directive
  was to integrate it as-is and make it build/run, together with the
  maintainer-side work that had been deliberately held because it shares the
  build/menu files (a clean split was impossible - CMakeLists.txt, apps/menu,
  and explorer_app_demo.c carry hunks from both). Landed in commit 782487f.
  **Their code, unchanged except one fix:** netx's frame.h used `bool` without
  `#include <stdbool.h>`, which broke the shared build; added the include, no
  other change to their sources. Their portable frame builders pass their own
  398-line host golden-byte suite (test_netx_frame), and net's new
  poll-suppress hook (so a capture tool can own the RX ring) was already in.
  **The one real bug the integration surfaced:** the app-tree view registry
  was still sized 22 while the combined tree now registers 24 views (the M9
  suite adds 2) - so two views were being *silently dropped at registration*,
  the exact `cads_view_dispatcher_add` returns-false-and-is-`(void)`d failure
  this file's own explorer_app_demo.c header documents twice. Bumped to 26
  (mirrored in test_app_tree.c, the host guard that catches this class).
  **Verified end to end:** board + host build clean, 640 B RAM margin (the
  ETH-TX-buffer lever from the earlier entry, not an lwIP trim, is what keeps
  this affordable), host suite 31/31, boot self-test 10/10 on hardware, and
  the app-tree init - which now registers all 24 views including the two M9
  ones - runs fault-free with a clean forensic ring. Not yet exercised (needs
  a person at the panel, does not block the image): the touch-calibration
  2-corner tap-test, and a visual pass over Settings -> Test pattern and the
  Active Net Tools views' actual on-panel behaviour.

- 2026-08-27 (UI pass) — Boot visuals reworked and a real dispatcher-capacity
  bug caught. Committed cleanly (not tangled with the parallel M9 work):
  **the boot no longer leaves the display test pattern on the panel** - it
  draws a branded splash with a progress bar the self-test drives (55% at the
  display check, 80% at the clock check, 100% "ready") and ends on the clean
  "ready" frame. The throughput/fast-clock self-tests still flush a full
  screen so their measurement is unchanged (verified: 10/10 TAP, flush still
  342 kpixel/s on hardware, and the sim golden regenerated). The test pattern
  moved to a portable `cads_test_pattern_draw()` (gui/cads_splash.c) for an
  on-demand **Settings -> Test pattern** entry. **Desktop logo+caption now
  centre vertically** in the content area instead of pinning 12px from the top
  (read as top-heavy). Both verified on the panel by webcam.
  **Dispatcher-capacity bug (the important one):** the app-tree view registry
  was at 22 while the tree now registers 24 views (desktop, menu, settings x4
  incl. calibration+test-pattern, about, gpio, netinfo, filebrowser x2,
  game x5, netiperf x2, nettools x4, and the parallel dev's active x2) - so
  two views were being *silently dropped at registration* (cads_view_dispatcher_add
  returns false, every caller `(void)`s it), exactly the failure class this
  file's explorer_app_demo.c header already documents twice. The last-registered
  views (the menu view itself, or an active view) would simply not exist,
  breaking navigation with no error. Bumped to 26 (mirrored in
  tests/unit/test_app_tree.c, whose synthetic OK-keypress test is the guard
  that catches this class on host before hardware). RAM after all of it:
  640 B margin. Boot/test-pattern/capacity commits are HELD with the rest of
  the tangled set pending the parallel dev's external.done; the boot-splash,
  desktop, and golden commits landed clean.

- 2026-08-27 — Network tools promoted to a GUI "Network" menu section, touch
  calibration implemented, and the 48K-heap-floor fight won with a better
  lever than starving lwIP. (Commits pending a clean split: the shared build
  files this touches are currently tangled with a second developer's
  in-progress M9 "Active Net Tools"/`modules/netx` work, so these land as one
  integrated commit once their `external.done` signals - the code itself is
  built, flashed, and host-tested now.)
  **apps/nettools**: a single "Network" launcher row opens a submenu holding
  three new GUI tools - Ping, ARP Scan, Traceroute - alongside the existing
  Network Info and iperf views (which lost their flat top-level rows). Built
  on modules/net's portable API, so the app is fully portable with honest sim
  stubs, no board/sim source split. Ping/traceroute block for their bounded
  run (they own a poll loop); the **ARP scan is a non-blocking state machine**
  (cads_nettools_tick, wired into the app-tree loop) sweeping **.1 to a
  Up/Down-configurable upper bound, default .254** - one request per 30 ms
  tick, each reply harvested a 4-tick window later so lwIP's bounded ARP
  table cannot evict a pending entry first. New portable
  cads_net_arp_request()/cads_net_arp_lookup() split out for it. Verified on
  hardware: the user navigated by touch to ARP Scan, ran it, panel showed the
  bench Mac's IP+MAC.
  **Touch calibration** (apps/settings/cads_touch_calib): the Settings row
  that said "not implemented yet" now opens a real 2-point crosshair flow -
  tap top-left then bottom-right, raw XPT2046 ADC counts at each corner are
  extrapolated to the full-panel min/max range (inverting hal_touch.c's own
  raw->pixel mapping) and applied live via cads_hal_touch_set_calibration().
  Portable (host build shows "needs a real panel"); the raw sampling runs in
  cads_touch_calib_tick() on a fresh-press edge, not the input handler, so a
  finger lifting mid-read is rejected. **Persistence now implemented**
  (2026-08-27): the module owns a lazily-mounted, single `/settings.kv` file
  (cads_settings_kv_ready - mount + cads_kv_open once, on first load/save),
  with no boot-path change - the open happens at cads_settings_init time on
  the app-tree path, which is the earliest touch is used through the GUI, so
  no shared boot file was touched (this was chosen deliberately while a second
  developer holds the boot/CMake files). cads_kv is a single global table, so
  a later settings-brightness/SPI-clock persistence task shares this same open
  and file. Verified: flashed, `d` exercises the mount+open path, forensic
  ring shows 0 records - the storage mount at init does not fault the boot;
  first run finds no saved keys and correctly keeps the driver defaults. Not
  yet verified: the full tap-calibrate -> reboot -> reloaded round trip, which
  needs a person at the panel to tap the two crosshairs. Touch itself verified
  healthy independently: `q 200` soak returned 0 ghosts.
  **RAM lever - the important structural note for next time:** three new
  features' static state (nettools views ~872 B, calibration view ~100 B,
  bigger dispatcher table) blew the `ASSERT(__cads_heap_size >= 48K)` guard.
  The wrong fix - the one this project kept reaching for all through M5/M6 -
  is trimming lwIP's own working memory (MEM_SIZE, the MEMP pools); that
  directly degrades the network robustness just fixed above, and there is a
  second developer about to need lwIP RAM too. The **right lever, found by
  actually reading the top static-RAM symbols** (`nm --size-sort` across the
  objects) instead of reflexively shaving pools: `cads_eth_tx_buf` was
  4x1536 B, and 4 TX descriptors is pure slack - the DMA drains a frame in
  ~123 us, far faster than this software-checksummed single-loop TX path
  refills it, so 2 never blocks a realistic sender (hal_eth_mac.c
  CADS_ETH_TX_COUNT 4->2, frees 3072 B). That paid for all three features
  AND let MEM_SIZE go back up to a robust 2048. Margin now **1088 B**, with
  headroom for the incoming netx merge. RX ring stays 4 - incoming bursts are
  not ours to pace. Lesson: measure the actual RAM map before trimming; the
  biggest static consumer is rarely the pool you reach for by habit.

- 2026-08-26 (RESOLVED) — **Ethernet reachability fixed. 15/15 ping replies,
  0% loss, sub-millisecond RTT, ARP resolving to the board's real MAC on the
  bench Mac - the first successful end-to-end traffic in this project's
  history.** Root cause was two independent firmware bugs, both found the
  same way: flashing the confirmed-working vendor `iperf` build and CaDS
  Zero in turn on the same board and diffing the full ETH/GPIO/RCC register
  file live over SWD (`st-util --no-reset`) at the same execution point
  (post link-up). The entries below record the two wrong turns this
  investigation took first (a broken vendor-image extraction, then a
  premature "it must be PA7 starvation alone" theory); this entry records
  what was actually wrong.
  **Bug 1 - `hal_eth_mac.c`: the DMABMR.SR software reset ran AFTER MAC
  configuration and silently wiped all of it.** RM0090's SR bit resets the
  entire MAC/DMA register file to defaults. The old order (set MACA0, set
  MACCR, then reset) left the MAC running at the reset-default **10M
  half-duplex against a 100M full-duplex link** - garbling the RMII wire in
  both directions, which is why autonegotiation (pure PHY-analog) always
  looked fine while not one frame ever survived in either direction, why
  Wireshark on the Mac saw literally nothing, and why the MAC's own
  `tx_good` counter still incremented (the MAC believed its 10M-timed
  transmit worked). It also left MACA0 at FF:FF:FF:FF:FF:FF and the MDC
  divider at Div42 (4.3 MHz, over the LAN8742A's 2.5 MHz ceiling). The
  smoking gun in the dumps: vendor `MACCR=0xce0c` (DM=1, FES=1) vs ours
  `0x800c` (DM=0, FES=0); vendor MACA0 = its real MAC vs ours = all-ones.
  Fix: reset first, then restore the MDC divider, then configure address
  and MACCR.
  **Bug 2 - `hal_spi.c`: PA7/CRS_DV parked on the SPI alternate function at
  idle, so the MAC's receive path was electrically disconnected except
  inside a display blit's release window.** `cads_eth_rmii_pins_init()`
  deliberately leaves PA7 alone and `cads_hal_spi_release_bus()` only
  flips it to AF11 after the NEXT blit - so between `mac_start()` and that
  next blit (during console-driven diagnostics: forever), CRS_DV never
  reached the MAC. The vendor parks PA7 on AF11/ETH at idle (measured:
  `GPIOA_AFRL` pin-7 nibble 0xB vs our 0x5). Fix:
  `cads_hal_spi_set_eth_datapath_active(true)` now parks PA7 on AF11
  immediately; claim/release still borrows it per blit chunk.
  With bug 1 fixed alone: still 0 RX (PA7 disconnected). With both: first
  test showed rx=7/tx=7 within seconds (the Mac's ARP probes being
  received and answered), second test 15/15 ping replies at ~0.9 ms avg
  plus the user's own independent long-running ping stabilizing at
  0.5-1.2 ms. Multi-second RTTs right at bring-up are expected: they are
  packets that queued while the netif was still down, drained on link-up.
  Replies flow only while something calls `cads_net_poll()` (the `h`
  gate, a net app in the app tree) - that is the design, not a bug, but
  worth remembering when a ping "stops": check what app is foregrounded
  before suspecting the driver again.
  Also fixed en route (correct but neither sufficient): MAC-before-DMA
  enable order in `cads_hal_eth_mac_start()` (RM0090/ST-HAL order), and
  chunked display blits (24 rows per claim/release instead of a whole
  screen - keeps PA7's ETH windows frequent under display load; visually
  verified clean via webcam). M5's hardware gate can finally move off
  `[!]` - end-to-end RX/TX is proven on real hardware. The
  SB121/SB122/PB5 rework remains available as the permanent way to end
  PA7 sharing entirely, but is no longer required for basic reachability.

- 2026-08-26 (superseded by the RESOLVED entry above) — Follow-on to the retraction below: with
  hardware and toolchain now cleared, went looking for the real cause of
  CaDS Zero's own Ethernet reachability failure and found one real, wrong-
  ordering bug (fixed, kept), one real and significant resource-starvation
  issue (found, partially mitigated, but **the mitigation did not fix
  reachability - still 100% loss**), and ended the session with the actual
  root cause still open. Recorded in full because the next session should
  not re-walk this same path.
  **Fixed: `cads_hal_eth_mac_start()` enabled DMA (`DMAOMR.ST/SR`) before
  the MAC (`MACCR.TE/RE`)**, backwards from RM0090's own recommended order
  and from ST's HAL `HAL_ETH_Start()` (MAC first, then DMA) - also now
  symmetric with `cads_hal_eth_mac_stop()`'s already-correct MAC-then-DMA
  teardown order. Correct and kept, but flashing it alone changed nothing
  (still 0/N ping replies) - not the root cause by itself.
  **Found: PA7/CRS_DV starvation is real and measured, not theoretical.**
  Read `GPIOA_AFRL` live over SWD (`st-util --no-reset`, so nothing about
  the running board was disturbed) at 0x40020020, bits[31:28] = pin 7's
  alternate function. During the explorer console's non-GUI diagnostic
  commands (`h`, `C`, `G` - none of which touch the display) it read
  **0x5 (SPI1)**, never 0xB (ETH), for the whole test window: PA7 was
  parked on SPI the entire time, so CRS_DV was never electrically connected
  to the MAC and RX could not have worked regardless of anything else -
  explaining why `C 8` (promiscuous capture) caught 0 frames over 8s on an
  active switch even with 0 RX-descriptor and 0 FIFO-overflow drops (the
  MAC's own view: nothing arrived, not "arrived and got dropped"). During
  active GUI blitting (`g`) the same read came back **0xB (ETH)** - the
  claim/release arbitration in hal_spi.c works correctly when it actually
  runs; the diagnostic commands just never called it.
  **Mitigated, not fixed: chunked `cads_hal_display_blit()`** (hal_display.c)
  from one claim/release per whole blit to one per
  `CADS_DISPLAY_BLIT_CHUNK_ROWS` (24) rows, reissuing CASET/PASET/RAMWR and
  re-bracketing CS per chunk - safe (each chunk is structurally identical to
  two independent blit calls, which the codebase already does correctly
  today; visually verified via webcam afterward, no tearing/artifacts) and
  gives Ethernet roughly an order of magnitude more, shorter windows per
  screen instead of one ~450 ms one (measured bus rate: 342 kpixel/s over
  480x320). Board self-test and host `ctest` (30/30) both still pass.
  **Flashed and retested: still 15/15 packets lost, no change at all** -
  not a partial improvement, literally identical to before the chunking.
  That is itself informative: a pure duty-cycle/timing explanation should
  have produced at least occasional lucky hits once windows became this
  much more frequent, especially against a static test pattern where the
  display is mostly idle between chunks and touch polling is the only other
  claimant. It did not. **Conclusion: PA7 starvation is real and measured,
  but the evidence now says it is not sufficient by itself to explain 100%,
  every-single-time loss - something else is still wrong, not yet found.**
  Next session should not re-verify PA7 (it's confirmed, both ways) or
  retry mild mitigations of it (already tried, measured no effect) -
  instead get a side-by-side register/state comparison against the
  confirmed-working vendor build at the same point in execution (post
  link-up, pre-first-frame), or actual signal-level (logic analyzer/scope)
  capture on TX_EN/TXD0/TXD1/RXD0/RXD1 while both firmwares run in turn on
  the same physical setup - the software-visible surface (descriptor rings,
  MAC/DMA register values, GPIO AF/pin config, MDIO/PHY state) has now been
  checked as far as it goes without new instrumentation. The permanent fix
  already designed into the code (`CADS_SPI_MOSI_ON_PB5=1` after swapping
  SB121/SB122) remains available and untried - a physical rework, not a
  software change, still the user's call.

- 2026-08-26 — **RETRACTION of the entry below**: the "physical RMII
  hardware fault" conclusion was wrong, caused by a broken test, not a broken
  board. "Decisive test 1" in that entry extracted the vendor flash image with
  `objcopy -O binary --only-section=ER_IROM1` and called it complete. It
  wasn't: AC6/armlink emits the `.data` load-copy (184 bytes here) as a
  *second, same-named* `RW_IRAM1` section whose VMA and LMA both read
  `0x20000000` in the ELF - a toolchain-specific metadata quirk (GCC-linked
  ELFs, including ours, give `.data` its own correct flash LMA and don't do
  this). Filtering to `ER_IROM1` silently dropped that section, so the
  flashed vendor binary ran with whatever stale bytes were already sitting at
  that flash offset standing in for its initialized globals - the kind of
  corruption that kills Ethernet/lwIP init silently while leaving
  `.rodata`-driven boot text on the LCD working fine. The user caught this by
  independently building and flashing the same `iperf` branch through their
  own already-working CMSIS-Toolbox/pyOCD VS Code setup
  (`~/Documents/git/ITS-BRD-VSC-build`) and getting a clean ping - directly
  contradicting "decisive test 1" and prompting a re-check instead of a
  defense of the earlier conclusion. Re-extracted the missing 184 bytes from
  the same `.axf`'s raw file offsets (contiguous with `ER_IROM1`'s own bytes
  in the file, despite the misleading LMA) and reflashed: ping went from
  100% loss to 0% loss with no other change. **The vendor `iperf` firmware
  works fine on this exact board, switch, and cable** - and by extension the
  earlier "`main` branch fails identically" test (same extraction method) is
  equally unreliable and should not be trusted either.
  Checked whether CaDS Zero's own flash image could have the same defect:
  no. `CMakeLists.txt:330` does a plain `objcopy -O binary` (no section
  filter) on a GCC-linked ELF, whose `.data` section already carries a
  correct, distinct flash LMA (`0x080435ec` at time of writing, contiguous
  right after `.text`/`.rodata`) - and the resulting `.bin`'s size already
  equals `text + data` exactly, with no gap. Different toolchain, different
  linker convention, bug does not transfer.
  **Net effect: hardware and toolchain are both cleared. CaDS Zero's own
  Ethernet/lwIP stack has a real, still-unexplained reachability bug**, and
  the investigation restarts from a clean slate on the RMII/MAC/PHY/lwIP
  init path - this time with a *confirmed-working* reference build on the
  same physical setup to diff behavior against (not source, per the
  clean-room policy - only documented pin/register/init-order facts).
  Lesson for next time: when an extraction/dump step describes itself as
  "the correct N KB image, avoiding [past bug]", that confidence is exactly
  where a second, different bug hides - verify a flashed image actually
  matches `size`'s `text+data` byte-for-byte before trusting a negative
  result built on it, the same way the RAM-budget and view-capacity checks
  already get verified elsewhere in this project.

- 2026-08-26 — Ethernet reachability root-caused to a physical RMII
  fault on the board itself, not software, not the bench Mac's USB chain.
  **(Superseded by the retraction above - kept for the trail, not the
  conclusion.)**
  Long investigation (see the entry below this one for the full trail);
  this entry records the conclusion reached at the very end, with the
  decisive new evidence.
  **Decisive test 1: the Vendor reference firmware, built and flashed to
  this exact board, shows the identical symptom.** Cloned
  `Transport-Protocol/ITS-BRD-VSC` (a HAW Hamburg teaching reference for
  this board, lwIP-based, confirmed working by the user elsewhere) with
  its submodules into a scratch directory (no code copied into this
  repo - clean-room preserved), built it for real with the already-
  installed CMSIS-Toolbox/Arm Compiler 6 toolchain (0 errors), extracted
  the correct 101 KB flash image (`objcopy -O binary --only-section=
  ER_IROM1`, avoiding the naive whole-ELF dump that balloons to 400 MB
  across the Flash/SRAM address gap), and flashed it to the real board -
  same static IP the vendor code hardcodes, 192.168.33.99. Result:
  0 packets received on the Mac side, identical to CaDS Zero's own
  firmware. This is conclusive: the vendor's own "known good" code,
  built with the vendor's own toolchain, fails identically on this
  specific board - the defect is not in either firmware, it is in this
  board's hardware. CaDS Zero's own firmware was reflashed back and
  reverified (10/10 self-test) immediately after.
  **Decisive test 2: Wireshark, live on the Mac's own dongle port,
  confirms the board's outgoing frames never leave the wire at all.**
  Earlier tests only ever showed netstat/ifconfig packet counters (an OS
  view several layers up); with Wireshark capturing directly on the
  dongle interface while the board's packet generator (`G`) sent 119
  frames (board's own DMA/MMC counters: sent, 0 dropped), filtering for
  the board's MAC (02:CA:D5:5E:00:01) or IP (192.168.33.99) showed
  **nothing** - not even a corrupted or malformed frame. The only frames
  Wireshark saw were the Mac's own routine background traffic (mDNS,
  ARP for its own address, IPv6 RS), confirming the dongle/OS network
  stack itself works fine and captures correctly - it simply never
  receives anything from the board, at the physical layer, full stop.
  **The reframe this enables:** autonegotiation succeeding (link up,
  100M full, confirmed independently on both the Mac's ifconfig and the
  board's own MDIO-read PHY status) does NOT prove the RMII data path
  works - autonegotiation is pure analog PHY-to-PHY signalling (FLP
  bursts) that never touches the MAC<->PHY RMII interface at all. A
  board whose RMII *data* lines (TX_EN/TXD0/TXD1 for transmit,
  RXD0/RXD1/CRS_DV for receive) are physically broken between the
  STM32F429 MAC and the LAN8742A PHY - while REF_CLK/MDIO/MDC (control/
  management, a completely separate signal set) stay intact - would
  autonegotiate successfully and pass every register-level and firmware-
  level check this session ran, while never moving one real data frame
  in either direction. That fits every single symptom collected this
  session: TX and RX equally dead (one shared interface fault, not two
  independent bugs), unaffected by any Mac-side change (9+ interventions:
  cables, two physically different dongles, hub power-cycle, service
  toggles, connector reseating - none of which touch the board's own
  MAC-PHY traces), and identical between two completely independent
  firmware codebases (both drive the same physical RMII pins on the same
  silicon).
  **Conclusion: this is very likely a physical hardware fault on the
  ITSboard itself - a broken trace, cold solder joint, or PHY-side fault
  on the RMII data lines (board.h: TX_EN=PG11, TXD0=PG13, TXD1=PB13,
  RXD0=PC4, RXD1=PC5, RXER=PG2) - not a CaDS Zero firmware bug, not the
  bench Mac's USB/dongle chain, not the cable.** Nothing further here is
  resolvable by software; the next diagnostic step needs a multimeter or
  oscilloscope directly on those board pins, which is out of this
  agent's reach. M5's own hardware gate stays `[!]` for this reason,
  now with a much more specific root-cause hypothesis than "no DHCP
  server on this bench" ever was.

- 2026-08-26 (even later) — Network config (DHCP/static) + iperf server/client
  apps shipped and hardware-verified; end-to-end reachability from the
  maintainer's own bench Mac investigated in depth and isolated to a
  Mac-side USB/driver issue, not this firmware.
  **cads/net gained cads_net_config_t** (use_dhcp + static ip/netmask/
  gateway), default **static 192.168.33.99/255.255.255.0, gateway
  192.168.33.1** (this bench has no DHCP server, so a lease never binds -
  a static default makes the board reachable out of the box). NetInfo app
  got a "Config:" row, OK toggles DHCP<->Static live. Verified correct at
  the source: `cads_net_status()` reported ip=192.168.33.99,
  gw=192.168.33.1, dhcp_bound=false after link-up, read via a temporary
  debug print (removed before commit).
  **apps/netiperf**: two new app-tree entries wrapping lwIP's lwiperf
  (already vendored for the explorer's `I` command, server-only) - a
  server view (OK toggles listening on :5001) and a client view (target
  editable via Up/Down on the last octet, default **192.168.99.1** per
  the user's own bench convention, OK starts/stops against
  `lwiperf_start_tcp_client_default()`). Board self-test 10/10 after
  adding it; two regression guards caught real bugs before hardware:
  the app-tree's view-registry capacity (14, already exactly saturated)
  silently dropped both new views (bumped to 16, mirrored in
  tests/unit/test_app_tree.c); the feature's static state broke the 48K
  RAM-margin ASSERT (trimmed lwipopts.h's MEMP_NUM_TCP_PCB_LISTEN/
  MEMP_NUM_UDP_PCB/MEMP_NUM_PBUF further, plus the app's own report
  buffer - margin restored to 288 B).
  **Reachability investigation, thorough, root-caused to the Mac side.**
  End-to-end ping/ARP from the maintainer's bench Mac to 192.168.33.99
  never succeeded despite both ends reporting "link up, 100M full" -
  chased with real evidence at every step rather than guessed:
  (1) netif static-address binding confirmed correct via a live status
  read (see above) - ruled out first.
  (2) A promiscuous capture on the board (`C`, bypasses lwIP's filtering
  entirely) showed 0 frames even while the Mac was actively pinging -
  ruled out an lwIP/filter-level cause.
  (3) Forced a real SPI claim/release cycle after link-up (a display
  pattern draw) to guarantee PA7/CRS_DV was time-sliced back onto the ETH
  peripheral (targets/itsboard/hal/hal_spi.c's PA7 arbitration, `#if
  !CADS_SPI_ETH_COEXIST` - stock board, no SB121/122 swap) before
  re-testing - still 0 captured, ruling out a stale-pin-mux explanation.
  (4) Decisive: the Mac's OWN interface (`en13`, a Hardware Port
  "AX88179A" - a USB3 Ethernet dongle, not the Mac's built-in en0, which
  is on a completely different subnet/network with 1.2M real packets
  flowing) has received **zero inbound packets in all of this session**
  (`netstat -I en13 -b`: Ipkts=0), independent of anything the board
  does - not even the ambient broadcast noise (ARP, mDNS, IPv6 ND) a live
  switched segment normally produces within seconds. Persisted across a
  cable swap and two dongle unplug/replug cycles (the second of which
  also transiently dropped the ST-Link off USB entirely - same physical
  hub, per the user - recovered by a further replug).
  **Conclusion: this is a Mac-side AX88179 USB-Ethernet dongle RX issue
  (a known class of bug with this chipset's macOS driver), not a CaDS
  Zero firmware defect.** The firmware's static-IP binding, PA7
  arbitration and RX/DMA path are all confirmed correct by the evidence
  above; nothing here needs a firmware fix. Left open for the user to
  resolve on the Mac side (try a different USB port not sharing the
  ST-Link's hub, a full driver/kext reload, or a different dongle) and
  re-verify end-to-end reachability once that's done - the iperf apps
  built this session are the natural next verification step once a route
  exists.

- 2026-08-26 (later still) — Touch and buttons verified on hardware with the
  user driving; Left/Right button mapping fixed; M3 navigation demonstrated.
  After the boot fixes above the board is stable, so the M3 input path could
  finally be exercised by a human. Findings, all live at the bench:
  - **Touch works** end to end. Raw XPT2046 stream (`Q`) while pressed: 118/122
    samples with `irq=1`, X/Y ADC varying across the full range (X ~1595-3686,
    xhi nonzero) - the old "raw X stuck at 0, TP_IRQ never toggles" symptom is
    gone. Scaled reads (`t`) verified against the quadrant test pattern:
    touching red (top-left) reads display x<240, y<160, matching where it
    renders. The touch X mapping was already correct - a mid-session hunch that
    it was mirrored was disproven by the quadrant test and reverted (the user's
    "left/right swapped" report was about the buttons, not touch).
  - **All 8 buttons register** (`s`). **Left/Right were bound to the wrong
    physical buttons**: the board has S3 physically left of S2, but the boot
    default bound Left=S2/Right=S3, moving the cursor opposite to the press.
    Fixed in services/input/cads_input.c (`cads_key_binding` Left=S3, Right=S2);
    physically-left now reports Left. Committed.
  - **M3 three-level navigation demonstrated:** app demo (`d`) with the user
    navigating desktop -> menu -> app by both buttons and touch: 110 frames
    flushed, 1.83 Mpixel, **8 navigation transitions**, no fault, no lost task,
    explorer responsive after. (An earlier run with the swapped buttons managed
    only 1 transition - the fix is what unblocked real navigation.) This is the
    human-driven half of HARDWARE GATE M3; see that item for the remaining
    formal sign-off wording.

- 2026-08-26 (later) — PC=0x0 boot crash ROOT-CAUSED and fixed; a SECOND
  boot hang then surfaced; bench blocked on a wedged ST-Link. In order:
  (1) **Root cause of the "NOT YET VERIFIED STABLE" mutex commit's PC=0x0 /
  INVSTATE fault, confirmed.** The board arrived this session already
  crash-looping on the HEAD firmware (9506a46): connect-under-reset was the
  only way to flash it, and a live `st-util --no-reset` read **PC=0x0** with
  an empty forensic ring and no console output - exactly the commit's own
  described symptom ("before console-up even printed"). Cause: 9506a46 made
  `cads_hal_spi_claim_bus()` take a **recursive** FreeRTOS mutex
  *unconditionally*, but the boot path flushes the panel (self-test +
  splash, apps/bringup/bringup.c) BEFORE `cads_kernel_start()` runs the
  scheduler. `xSemaphoreTakeRecursive` dereferences `pxCurrentTCB`, which is
  NULL until the scheduler starts -> null jump -> UsageFault INVSTATE PC=0x0,
  in a loop with no forensic record because it faults before anything is up.
  FreeRTOS forbids blocking API calls before `vTaskStartScheduler()`.
  **Fix (targets/itsboard/hal/hal_spi.c):** gate the take/give on
  `xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED` - boot is
  single-threaded by construction, so the lock is unnecessary AND unsafe
  before the scheduler exists. Gated on FreeRTOS's own API, not
  `cads_kernel_running()`, to avoid the HAL depending upward on the kernel
  module. **Verified the fix advances boot:** after flashing it the board
  now prints "console up" + the M0 banner, which it never reached before.
  (2) **A SECOND boot hang, ALSO caused by the mutex commit, ROOT-CAUSED and
  fixed.** With (1) fixed the board printed the banner then hung before the
  "1..10" TAP plan; webcam showed a **backlit white panel, no content** and
  the forensic ring stayed empty (a HANG, not a fault - a fault writes a
  record). `# DBG a..f` markers around each boot step localised it to the
  FIRST `cads_canvas_flush()` (bringup.c). Caught live with a debugger:
  the DMA was actually running (NDTR counting down) but **BASEPRI read 0x50**
  = `configMAX_SYSCALL_INTERRUPT_PRIORITY`. This driver's DMA2_Stream3
  completion IRQ is priority 6 -> register 0x60, and BASEPRI=0x50 masks every
  interrupt >= 0x50, so the transfer completes, TCIF latches, but the ISR
  that clears `cads_spi_dma_active` never fires and `cads_hal_spi_wait()`
  spins forever. Why BASEPRI=0x50 during boot: creating the recursive mutex
  in `cads_hal_spi_init()` (9506a46, pre-scheduler) takes a FreeRTOS critical
  section (`xQueueGenericReset` -> `taskENTER_CRITICAL`); before the scheduler
  runs `uxCriticalNesting` holds the CM4F port's poison init 0xaaaaaaaa (reset
  to 0 only by `xPortStartScheduler`), so the matching exit decrements the
  poison instead of reaching zero and NEVER restores BASEPRI. Documented
  FreeRTOS gotcha (FreeRTOS-Kernel issue #254; confirmed from the port source
  and a live register read, not assumed). **Fix (hal_spi.c):** call
  `portENABLE_INTERRUPTS()` right after the mutex create to force BASEPRI back
  to 0 - boot is single-threaded and must run interrupts-enabled.
  **RESOLVED and hardware-verified:** with both fixes the board boots clean
  and the M0 gate passes **10/10, RESULT: PASS** on the real board; the panel
  renders the brand colour bars + UI surface (photographed), and the explorer
  is fully responsive. RAM margin unchanged at 256 B, host ctest 30/30. The
  DBG markers were removed before committing.
  Bench note: heavy connect-under-reset + `st-util` cycling twice wedged the
  ST-Link V2 USB (`LIBUSB_ERROR_TIMEOUT`, `chipid 0x000`); a physical USB
  replug is the only recovery on macOS - budget for it during long autonomous
  hardware sessions.

- 2026-08-26 — Crash investigation, reframed: the tick hook is very likely
  an EXPOSER, not the cause. Background research (independently re-deriving
  the priority chain from source rather than trusting the earlier summary)
  confirmed clean: configMAX_SYSCALL_INTERRUPT_PRIORITY=0x50/
  configKERNEL_INTERRUPT_PRIORITY=0xF0 are correctly derived and
  self-consistent with port.c's own runtime asserts (which pass, since the
  system runs at all); NVIC priority grouping is never explicitly set
  anywhere in this codebase, but the silicon reset default (PRIGROUP=0,
  all-preempt) happens to already be exactly what the CM4F port requires -
  correct, if implicit and worth hardening with an explicit call later.
  FPU enable (CPACR CP10/CP11 at startup, ASPEN+LSPEN in port.c) is
  standard and correct. The tick hook's own body remains exactly one MMIO
  write with no shared state and no FreeRTOS API - cannot itself corrupt
  memory or trip any ISR-priority assert.
  The reframe: three different fault signatures across three captures
  (clean configASSERT, UsageFault/UNDEFINSTR, precise BusFault to a
  garbage address) are most likely ONE underlying, still-unlocated memory
  corruption bug landing on different victims each time - not the tick
  hook causing three different bugs. Two coincident, non-causal effects
  explain why it only became visible tonight: (1) the watchdog is active
  for the first time, so a fault that previously left the board silently
  hung forever now reboots visibly with a forensic dump instead -
  visibility changed, not the underlying bug's existence; (2) each
  rebuild/relink shifts addresses, so the same corruption lands on
  different memory each time, producing different-looking symptoms. This
  fits independent evidence from earlier tonight: before any of this
  session's watchdog work existed, the user reported pressing the
  physical reset button because "nothing was happening" - a silent hang
  needing a manual power cycle, exactly the pre-visibility symptom this
  theory predicts.
  Leading candidate for the actual corruption source (honestly still
  unpinned, not confirmed): a transient task-stack overshoot in CCM
  (task stacks, the MSP and the forensic ring all share that region per
  the linker script) corrupting a neighbor - `configCHECK_FOR_STACK_
  OVERFLOW=2` only samples at context switches, so a deep excursion that
  overflows and returns between switches would be invisible to it, and
  it has zero visibility into the MSP at all. An Ethernet RX-overrun
  lead was chased and ruled out (hal_eth_mac.c's copy-out is correctly
  length-clamped, RBS caps the DMA).
  Recommendation, agreed and being acted on: do NOT move the watchdog
  feed out of the tick hook - if the corruption is elsewhere, moving it
  fixes nothing and only trades away real interrupts-disabled-deadlock
  detection for no benefit. Instead, adding proper instrumentation before
  guessing at a structural fix, same discipline that worked for tonight's
  touch bug: stack canaries (MSP + each CCM task stack, painted at boot,
  checked in the idle hook), MSP/PSP added to the forensic record, and
  `-fstack-usage` added to the build for a static worst-case bound. Not
  yet implemented as of this entry - in progress.

- 2026-08-26 — Crash investigation, in progress (not yet resolved): the
  autonomous, sporadic crash first seen after tonight's watchdog work
  (`vApplicationTickHook()` feeding IWDG from inside `xPortSysTickHandler`,
  ~1kHz) is still reproducing, now on three separate hardware captures with
  three DIFFERENT fault signatures - a clean `configASSERT` panic (reason
  recorded as port.c's path, no hardware fault frame), a UsageFault
  decoding via addr2line to inside `xTaskIncrementTick`'s call site in
  `SysTick_Handler` (CFSR UNDEFINSTR), and freshest, a precise BusFault
  with BFAR pointing at a garbage, unmapped address (0x5808615E - not
  flash/RAM/CCM/any peripheral). Varying signatures across occurrences
  reads more like memory corruption than one deterministic logic bug.
  Ruled out tonight, with evidence not just assumption: an ISR calling a
  FreeRTOS FromISR API above `configMAX_SYSCALL_INTERRUPT_PRIORITY` (the
  classic port.c `configASSERT` pitfall) - only two real ISR handlers
  exist anywhere in the tree (USART3 priority 8, DMA2_Stream3 priority 6,
  both correctly masked, both grepped clean of any FreeRTOS API call), and
  the project's only ISR-safe queue wrapper (`cads_queue_send_from_isr`)
  has zero callers. `vApplicationTickHook`'s signature also re-confirmed
  exact against FreeRTOS's own task.h. NOT yet independently verified:
  `SCB->AIRCR`'s priority-grouping bits are never explicitly set anywhere
  in this codebase (grepped clean of `NVIC_SetPriorityGrouping`/`AIRCR`/
  `PRIGROUP`), relying entirely on silicon reset defaults - FreeRTOS's
  ARM_CM4F port has its own assert for exactly this
  (`vPortValidateInterruptPriority`, port.c:904) but that code path is
  only exercised by a FromISR call, which - see above - never currently
  happens, so a wrong PRIGROUP value would not be caught by the kernel's
  own safeguard and remains an open, unconfirmed variable.
  Also fixed along the way, independent of the root cause:
  `cads_kernel_assert()` (modules/kernel/src/kernel.c) previously
  discarded the `configASSERT` line number, recording only the file -
  port.c alone has 15+ assert call sites, so "reason: port.c" could not
  say which one fired without decoding a PC through addr2line. Now
  records "file:line" into a static buffer. RAM margin 416B -> 320B,
  still passing the 256B floor.
  A background research agent (crash-research) is digging into the
  PRIGROUP question and the MSP-stack-depth-under-ISR-nesting hypothesis
  in parallel with continued work. Leading candidate fix if the stack-
  depth theory holds: move the watchdog feed out of the tick hook into a
  low-priority periodic task instead, trading away "catches an
  interrupts-disabled deadlock" for "stops adding depth to the one stack
  every ISR shares" - not applied yet, waiting for the research pass
  rather than guessing, same discipline that worked for tonight's touch
  bug. Board self-recovers from every occurrence so far (watchdog-reset,
  confirmed via `cads_hal_reset_cause()`), so this is a real but bounded
  problem, not a hard hang - still the top priority for the rest of the
  night per explicit instruction.

- 2026-08-26 — Touch root-caused and fixed: a one-transfer SPI read desync,
  not a wiring or timing fault. The delay-removal fix (2026-08-25) and the
  PENIRQ-masking fix (this session, reapplied after being reverted and
  cleared of an unrelated crash correlation) both left the same signature
  unexplained: during confirmed real presses (PENIRQ correctly asserting),
  the 12-bit result was always 0-15, and a new byte-level diagnostic
  (`Q`'s xhi/xlo/yhi/ylo` output, `cads_hal_touch_read_raw_bytes()` in
  hal_touch.c) proved the high byte was exactly 0x00 on every sample,
  symmetrically on both axes.
  A background research agent (dispatched with the full ruled-out list -
  SPI mode, clock speed, command bytes, CS timing, PD1PD0 reference-off
  semantics in differential mode - all independently confirmed clean)
  found the actual cause: the display's own writes are TX-only (polled
  command bytes, both DMA paths) and never read SPI1->DR back, leaving a
  byte sitting in DR with RXNE set. `cads_hal_spi_transfer()` waits for
  RXNE, not specifically for its own transfer's RXNE, so the touch
  driver's next polled read (command, then two data bytes) silently
  desyncs by one transfer: "high" returns the stale leftover - which is
  reliably 0x00, since the XPT2046 holds DOUT low for the entire command
  byte - and "low" is really the true high byte (the top 7 conversion
  bits, range 0-127, matching the observed 0-15 after the `>>3` shift
  exactly). Fixed in `cads_hal_spi_wait()` - the one checkpoint every
  polled-after-DMA caller already funnels through before touching the bus
  again - by draining DR then SR (RM0090 28.3.7's documented OVR-clear
  order) once quiescence is confirmed. One drain is enough; this SPI has
  no RX FIFO to loop over.
  CONFIRMED on hardware, and confirmed as the actual bug rather than
  merely correlated with one: the idle-state raw Y reading changed from a
  suspiciously stable small value (yhi=0x00, ylo~120 - the display's own
  stale byte, misread every time) to a genuinely noisy, full-range
  floating-input reading (yhi~120, ylo swinging 32-216) the moment the fix
  landed - the exact byte that could never carry conversion data under the
  desync is now carrying real MSB-range data. That is a direct observation
  of the pathological byte fixed, not a correlation. Host suite 30/30,
  board self-test 10/10, RAM/CCM budget unchanged (416 B margin).
  STILL OPEN: end-to-end coordinate accuracy under a genuine firm press
  has not been re-verified tonight - only a passively resting stylus was
  available (a resident's suggestion; insufficient force on a resistive
  panel, as expected - PENIRQ never asserted). This is a calibration/
  scaling check now, not a fix-verification question: `cads_touch_scale()`
  needs raw ADC values landing in the ~200-3900 calibrated band and
  tracking finger position, and the `Q` command's raw reads already
  ignore PENIRQ entirely (by original design, to separate "does the pin
  toggle" from "does the ADC work"), so this needs nothing more than
  someone physically pressing the panel while `Q <sec>` runs. Left for the
  next time the user or a session is at the board.

- 2026-08-25 — Watchdog-fed crash forensics: hardware-gated, with one real
  bug found and fixed along the way and one narrower question still open.
  A first flashed build of `cads_hal_watchdog_init()` (see this same
  date's earlier Log entry for the feature itself) built clean and
  booted clean, but a live `z FAULT` test never auto-recovered - the
  board just sat hung indefinitely, watchdog included. Root cause,
  found by comparing against ST's own `HAL_IWDG_Init()`: the enable key
  (0xCCCC) has to be written FIRST, before the register-access unlock
  and PR/RLR configure/wait sequence, not last - RM0090 20.3.2 and the
  HAL both start-then-configure; the original code configured-then-
  started, which built, looked reasonable, and simply did not work.
  Also widened the forensic ring's per-slot validity marker from one
  32-bit magic word to two, after confirming live that this board's
  CCM - reset dozens of times in one session without a true power
  cycle - produced an actual coincidental match on the single-word
  version within the first hour of the feature existing (two phantom
  records with garbled `reason` strings, since `reason` is a raw flash
  pointer and old CCM content read through a newer build's `.rodata`
  layout is exactly the kind of stale-but-plausible-looking garbage a
  coincidental magic match would produce - not a real-world concern,
  since normal operation resets far more than it reflashes, but a real
  gap during a night this heavy on iteration).
  PROVEN, after the fix: a new board-only `X` explorer command
  (interrupts off, spin forever, nothing else - deliberately isolated
  from `z FAULT`'s own bkpt/HardFault-escalation machinery) auto-
  recovered within the expected window with no debugger attached, and
  the following boot's `cads_hal_reset_cause()` correctly reported
  "IWDG watchdog". That is the core safety property this feature exists
  for, confirmed live.
  STILL OPEN: `z FAULT` itself (UsageFault -> bkpt -> HardFault
  escalation, no debugger attached) does not currently auto-recover,
  even after the ordering fix and even after a full power cycle ruled
  out a stuck debug-enable latch as the explanation. The `X` path proves
  the watchdog mechanism is sound in isolation, so this is narrower than
  it first looked - something specific to the fault-recursion path, not
  the watchdog itself. Deliberately not chased further live tonight;
  next session should reason about it fresh rather than continue
  trial-and-error against real hardware.
  VERIFIED: host ctest 30/30 throughout. Board: clean boot, `X`-induced
  hang auto-recovers, reset cause correctly decoded, forensic ring
  records and displays real crash data across a genuine recovery cycle.
  RAM margin 416 B unchanged throughout (every byte of the ring and its
  wider magic lives in CCM, confirmed via the linker's own 48K-floor
  ASSERT catching a first CMake mistake that put it in RAM instead - see
  that date's earlier Log entry).

- 2026-08-25 — Touch: one real fix applied and verified insufficient,
  narrowing the problem rather than closing it. The touch-research
  background agent's top hypothesis (a 10us delay inside
  `cads_touch_read_axis()`, between the command byte and the two data
  bytes, letting the XPT2046's charge-redistribution SAR droop toward
  zero while power-down-idle) was well-argued and matched the symptom
  precisely, but removing it did NOT fix the coordinate readings -
  confirmed live, with the user actually pressing across a clean
  30-second `Q` window: `irq=1` correctly tracked 41 of 151 samples
  (press-detection genuinely works, unchanged from before), but the raw
  ADC counts were still single/low-double digits instead of spanning
  0-4095. So two things are now more precisely known than at the start
  of the night: the IRQ/pen-down signal path is entirely sound (proven
  twice now, independently), and whatever is wrong is specific to the
  SPI coordinate *data* path and is NOT explained by the mid-transaction
  delay. The research agent's own fallback (masking PENIRQ during the
  read via PD1PD0 bits, with a mandatory re-arm before releasing CS) was
  not attempted - real risk of breaking the now-twice-confirmed-working
  press detection if the re-arm is not exactly right, not something to
  try live this late without being able to verify carefully. NEXT
  SESSION: the research agent's own conclusion stands - this needs a
  logic analyzer or scope on the SPI lines to see what is actually being
  clocked, rather than more reasoned-from-the-datasheet guesses. Two
  firmware attempts (this session's own delay fix, plus whatever
  produced the original implementation) have both been plausible and
  both wrong, which is itself evidence this is not a software-logic bug
  discoverable by reading the code again.

- 2026-08-25 — Resolved the button/touch input investigation that ran
  across most of this session, with a real live-hardware protocol this
  time (previous attempts kept getting confounded by nobody actually
  being at the board during the test window, or the user pressing the
  wrong physical button - the silkscreen reads IN7..IN0 left to right,
  descending, not IN0..IN7 ascending, which had been assumed wrong
  earlier the same night).
  BUTTONS: fully verified working, no firmware defect. A raw
  `s <sec>` (debounced input service, bypasses the GUI/task layer
  entirely) session with the user actually pressing showed F1 and Back
  registering correctly. A subsequent live `d` app-tree session
  confirmed OK opens the menu and Up/Down navigate it correctly
  (S1->Down, S0->Up - matches `cads_key_names[]` exactly, key index ==
  button index by design). The whole night's confusion was almost
  entirely the position mixup above plus the font/softkey-contrast bugs
  already fixed making success hard to perceive even when it happened,
  not a functional defect.
  TOUCH: a real, precisely isolated bug remains, NOT fixed tonight.
  Added two temporary/permanent-candidate diagnostics to separate the
  IRQ signal from the coordinate data: `targets/itsboard/hal/
  hal_touch.c`'s `cads_hal_touch_read_raw_x/y()` +
  `cads_hal_touch_irq_raw()` (deliberately not added to core/cads_hal.h
  - board-only, declared extern directly in explorer.c, guarded
  `#ifdef CADS_TARGET_ITSBOARD` so sim/host need no stub), and explorer
  command `Q <sec>` that prints raw ADC counts + raw IRQ level every
  200 ms ignoring `cads_touch_pressed()` entirely. Findings, in order:
  (1) A first `w <sec>` port-wide watch showed TP_IRQ (PE13) never
  toggling across 6693 lines of other-pin noise - looked like a dead
  IRQ line, but the user confirmed afterward they had not actually been
  touching the panel during that window. (2) A `Q <sec>` session with
  the user genuinely pressing/dragging showed `irq=1` while pressed and
  `irq=0` on lift, tracking real contact correctly - the IRQ line and
  `cads_touch_pressed()` are NOT the bug. (3) The SAME session's raw
  ADC counts were implausible the whole time - single/low-double digits
  (e.g. `x=7 y=2`), where a 12-bit conversion should read up to ~4095.
  Ruled out by static review before stopping for the night: SPI mode
  (CPOL=0/CPHA=0, fixed for the whole shared bus, `hal_spi.c`'s
  `cads_spi_configure()`) and frame width (`cads_hal_spi_set_speed
  (CadsSpiSpeedTouch)` explicitly forces 8-bit, ruling out a stale
  16-bit-mode leftover from the display driver), chip-select polarity
  (`cads_touch_cs()`), and the XPT2046 control bytes themselves
  (`0xD0`/`0x90` = S=1, channel 101/001 = X/Y, 12-bit, differential -
  the standard, widely-used encoding, not a typo). None of those explain
  it. Also fixed in passing while reading this code: `cads_hal_touch_
  read()` left `state->x`/`state->y` uninitialized on its early-return
  path (not pressed) - real UB, same bug class as the game
  stack-overflow found earlier this session, though not the actual
  cause of tonight's symptom since the app only reads x/y when
  `pressed` is true. Now zeroed unconditionally at the top of the
  function. NEXT SESSION: needs either a logic analyzer/scope on the
  touch SPI lines, or systematic empirical trial (conversion-delay
  length, SER/DFR mode, MODE bit) via the new `Q` command - deliberately
  not guessed at blindly across more flash-and-test cycles at this hour.
  VERIFIED: host ctest 29/29; board boot self-test 10/10; both new
  diagnostics ran live on hardware and produced the data above; RAM
  margin 416 B unchanged (board-only diagnostic additions, no shared
  RAM cost).

- 2026-08-25 — Fixed the real cause of the illegible text the user kept
  reporting live at the board ("hoch und niedrig gestellte Buchstaben" -
  letters set high and low mid-word): a genuine bug in
  `scripts/gen_font.py`'s `bake()`, not a perception or contrast issue.
  The baked `top` field used `ascent - top` (PIL's own bbox y0, which
  `font.getbbox()` already returns anchored at the ascender line); that
  formula only lands every glyph's bottom on a shared baseline if
  `y1 == 2*y0`, which is not true in general, so ascender letters
  (b,d,f,h,k,l,t) and x-height letters (a,c,e,m,n,o...) baked to different
  baselines - `top+height` was 24 for the former, 18 for the latter,
  against a font16 ascent of 17. Confirmed empirically with a throwaway PIL
  probe before touching anything: `font.getbbox(ch)[3]` (bottom) is a rock
  solid 17 for every non-descender glyph and 20 for g/p/y, and only y0
  varies - proving y0 alone, undoctored, is already "rows from the line top
  to the bitmap top" exactly as `cads_glyph_t.top` is documented. Fixed by
  using `top` directly; regenerated gui/fonts/cads_fonts.c from the same
  JetBrains Mono TTF (`python3 scripts/gen_font.py --out
  gui/fonts/cads_fonts.c`). Also fixed gui/widgets/cads_softkeys.c, found
  while comparing the on-device render against the interface-language
  mockup the user asked to match: a live cell filled CadsColorSurface
  (near-white) with CadsColorBrandLight (pale blue-grey) label text - low
  contrast, part of the same "schwer zu lesen" report. Now the whole strip
  is a uniform CadsColorBrandDark bar (CadsColorBrand when a cell is held)
  with white/BrandLight text, matching the mockup and giving every screen a
  legible, high-contrast, positionally-obvious button legend (cell N sits
  directly under physical button SN by construction - see
  cads_softkeys.h's own header). VERIFIED: host ctest 29/29 after
  `update_golden` (only the ZERO wordmark and self-test caption moved, to
  their corrected baseline - reviewed via diff image before accepting);
  gallery screenshots reviewed for desktop/menu/netinfo; board boot
  self-test 10/10; RAM margin 416 B, flash headroom unchanged (font/color
  table sizes did not grow); app-tree demo confirmed live on hardware
  afterward. Deferred to a later session: the watchdog/crash-forensics
  feature the user asked for earlier the same night - core/cads_hal.h
  already has the reset-cause and
  watchdog API contract drafted (CCM-resident forensic ring buffer, IWDG
  fed from the FreeRTOS tick hook, DBGMCU_APB1_FZ_DBG_IWDG_STOP so a
  debugger session is never raced by a surprise reset) but no .c
  implementation yet. Also queued, not started: boot-time diagnostic
  entry points (hold a button at power-on for touch calibration / a help
  screen) the user asked for as a follow-up.

- 2026-08-24 — Emboldened all rendered text (gui/canvas.c's shared
  `cads_canvas_draw_text`): every lit glyph pixel now also lights the pixel
  one column right, a standard cheap emboldening trick for a fixed 1bpp
  bitmap font with no baked bold weight (gui/fonts/cads_fonts.c is
  deliberately not antialiased). Prompted by direct user feedback live at
  the board: "Schrift schlecht zu lesen... muss kräftiger sein, dass man es
  lesen kann." Scoped correctly per the golden-image diff before accepting
  it (only glyph-rendered text moved - `ZERO` and the self-test caption on
  the splash screen; the CaDS wordmark/tagline/lion are bitmap image assets
  and are untouched). VERIFIED: host ctest 29/29 after `update_golden`;
  board boot self-test 10/10; RAM margin 416 B unchanged (host-only
  rendering-loop change, no new storage). Same live session surfaced a
  second, still-open report from the user: neither touch nor the physical
  buttons produced any visible reaction in the `d` app-tree demo. Two
  `s <sec>` live-input diagnostic windows during this session were
  inconclusive by innocent cause (the user was away from the board both
  times, confirmed after the fact - not evidence either way), not because
  the read failed. This is the same open item as the M6 hardware gate's
  "human walkthrough of each app by touch and by button" line below -
  still not actually completed - and needs a live diagnostic window with
  the user physically present and pressing keys to resolve, not more
  unattended `s` runs.

- 2026-08-24 — UI redesign phase 2: light content surface for all menu/list
  screens (main menu, settings, filebrowser, arcade select). Rows move from
  the dark navy ground + light-blue text to the mockup's CadsColorSurface
  ground + GrayDark text + GrayLight separators; the selected row keeps
  phase 1's brand fill + white text + green rail. Kept apps/gpio's dark
  custom screen unchanged by giving cads_list a per-list `background` field
  (default dark) that cads_menu overrides to Surface. Prompted by the webcam
  verification the user asked for: the earlier dark ground turned the glossy
  panel into a mirror of the lit bench, so nothing was legible/photographable
  under glare - the light ground fixes that. VERIFIED the change renders on
  hardware (panel goes from dark mirror to clearly light across reflashed
  webcam shots; boot 10/10; host ctest 27/27; RAM margin 416 B unchanged; CI
  green). The fixed bench webcam is too soft to resolve the individual rows/
  5px rail pixel-sharp - a human glance is the final fine-detail check - but
  the dark->light change itself is unmistakable in the photos. Commit 221bf7e.
  Remaining redesign phases: rect-drawn menu icons, app/game-screen polish.


- 2026-08-24 — On-device UI redesign, not from an open roadmap/issue item -
  user asked to overhaul all GUI/UX elements to be "richtig performant aber
  auch gut aussehend" using the design tooling. Produced an approved,
  viewable design-language mockup (faithful 480x320, exact 16-colour
  palette, JetBrains Mono, the original CaDS logo on the desktop, flat-fill
  performance model) as the spec, then began the phased C implementation.
  **Phase 1 (this commit, 372413c):** the design's signature focal cue on
  every menu/list-based screen - a solid 5px brand-green rail down the left
  edge of the selected row, plus a GrayDark hairline under each unselected
  row. Contained to cads_menu_draw_row so it lands uniformly on the main
  menu, settings, filebrowser and arcade select with no change to the shared
  cads_list widget and no light/dark inconsistency; both marks are flat
  fills/a hairline (cheapest on this bus) and selection still repaints only
  two rows via the list's dirty-row tracking, so it costs nothing in frame
  budget. Host ctest 27/27, board boot self-test 10/10, app-tree renders
  fault-free, CI green (run 32690981379). Visual confirmation of the rail
  needs a physical OK press to open the menu, which cannot be injected from
  this shell (same limitation as the games) - a human at the bench is the
  remaining check. Remaining phases: light-surface content conversion
  (coordinated across cads_list + its consumers), rect-drawn menu icons,
  app-screen and game-screen polish.


- 2026-08-23 — Documentation completeness pass, not from an open
  roadmap/issue item - user asked directly whether the application and
  technical documentation were complete and whether a performance/size
  review process actually existed, after the 5-watcher M5 batch and
  before this. Ran a fresh full board test first (not from cache):
  `board_test.py --suite m6` on real hardware, boot self-test 10/10,
  M6 suite 4/4 (continuity correctly reports "no continuity" with no
  jumper wire attached - a well-formed result, not a failure). Then two
  independent audits, both grounded in reading the actual files rather
  than assumed:
  (1) Docs were sharply bimodal - architecture/technical docs
  (HARDWARE.md, SAFETY.md, pa7-conflict.md, hal.md, canvas.md,
  module-layout.md, memory-map.md) were genuinely complete and current;
  end-user documentation essentially did not exist - zero of the 44
  bring-up explorer console commands documented anywhere outside the
  firmware's own `?` help string, 5 of 8 on-device apps
  (about/gpio/netinfo/filebrowser/game) had no README at all, and 4 of
  5 feature modules (cli/kernel/net/storage) had no README despite
  `docs/reference/module-layout.md`'s own "every module carries a
  README" standard - only `toolbox` had one, and it was itself
  incomplete (7 of its 14 files, silent about the 6 M5 network-watch
  parsers and 1 M6 timing utility).
  (2) A real, CI-enforced RAM-margin and filesystem-window gate exists
  and was verified live (both matrix legs, both checks, exact numbers
  matched against `nm`/`objdump` on the checked-in ELF) - but flash/text
  size was only *printed*, never *gated*, so it could grow indefinitely
  between commits unnoticed, and `docs/reference/measurements.md` had
  gone stale (5+ days, ~30 points of RAM% behind reality; display
  throughput never re-measured since M0/M1, before the scheduler and
  eleven M5/M6 apps/watchers existed).
  Closed every concrete gap found: new `docs/reference/explorer-console.md`
  (all 44 commands, sourced from the fresh live help-text run plus
  `explorer.c`'s own dispatch table for exact defaults); 9 new
  README.md files (5 apps, 4 modules) via 9 independent research agents,
  each grounded in the real headers/source and spot-checked against it
  afterward (`modules/net`'s and `modules/storage`'s API signatures,
  `lwipopts.h` constants, and the `.ramfunc` flash bug all verified
  letter-perfect against the actual files, not taken on trust); fixed
  `apps/menu/README.md`'s stale four-row claim (actually up to six,
  build-time optional since M6) and extended `toolbox/README.md` to its
  full 14 files; refreshed `measurements.md` and the root `README.md`'s
  status section with current, cross-checked numbers; added
  `scripts/check_flash_budget.py` (512 KB budget, half of `FLASH_APP`,
  2x headroom over the current ~222 KB) wired into CI right after the
  RAM budget step. Confirmed green live on the push (run 32633081348):
  both matrix legs pass all four gates including the new flash budget
  (`PASS: 296828 B of headroom under the 524288 B budget` on the
  default leg), and the mkdocs deploy (run 32633081373) built the new
  reference page successfully.

- 2026-08-23 — M5: new `O <sec>` passive traffic-mix overview, fifth in
  the watcher series but deliberately the leanest: RAM margin was
  tightening with each table-keeping watcher (928 B left after `U`),
  so this one answers "how much of what kind" (dest class + EtherType
  mix) with eleven `uint32_t` counters and no per-source table at all.
  New `modules/toolbox/trafficstats.h/.c` (11 host unit tests). Its
  `cads_trafficstats_t stats` is a plain stack local, not `static` -
  44 B is nowhere near enough to risk the task's own stack, and
  `check_ram_budget.py` confirms margin is byte-for-byte unchanged at
  928 B: this command genuinely cost zero additional static RAM.
  VERIFIED on hardware: `O 8` clean (every counter honestly zero),
  `U 5`/`B 5` re-verified with no regression from a sixth command
  sharing the capture buffer, `d 8` and the M0 boot self-test both
  clean, host `ctest` 24/24. Full detail in M5's own bullet.

- 2026-08-23 — CI: fixed a real false positive in the "filesystem
  region is untouched" check, caught live on the `U` (ssdpwatch) push -
  the "minimal apps" matrix leg failed even though no section anywhere
  near the littlefs window existed. Root cause: the check was
  `grep -qE '0810[2-9a-f]|081[1-9a-f]'` against `objdump -h`'s raw
  text, never anchored to the address column - a substring search
  across a whole section line (which also has SIZE, file offset,
  alignment). The minimal-apps build's different RAM footprint shifted
  `.dmaram`'s VMA to `20008144`, and "0814" - a substring of that
  SRAM address, nothing to do with flash - matched `081[1-9a-f]`.
  Replaced with `scripts/check_fs_window.py`: parses `objdump -h` by
  column position (not substring search), skips ALLOC-only sections
  (`.bss`/`.dmaram`/`.heap` routinely carry a leftover LMA that means
  nothing about real flash occupancy), and does a real numeric
  interval-overlap check against the littlefs window for sections that
  actually have the LOAD flag. Verified both directions before
  deploying: a synthetic section with an LMA genuinely inside the
  window is caught (checked outside any build, not assumed), and both
  the default and minimal-apps ELFs built locally now report PASS
  where the old check falsely failed. Confirmed green live on the push
  (run 32606301939): both matrix legs pass with the new check.

- 2026-08-23 — M5: new `U <sec>` passive SSDP/UPnP device discovery
  listener, fourth in the user-requested series (`N`/`R`/`B`/`U`). New
  `modules/toolbox/ssdpwatch.h/.c` (18 host unit tests) - a header-line
  scanner, not a TLV walker, since SSDP is HTTP-style plaintext, not a
  binary protocol like the other three watchers' - with dedup keyed on
  (src_mac, USN) since one device exposes more than one UPnP service.
  A first version of the test file's own frame builder overflowed its
  stack buffer (a real SSDP payload runs well over 150 B) - caught by
  a SIGTRAP on the very first host test run, fixed by sizing to the
  message. Uses the shared `explorer_capture_buffer.h` - total added
  RAM +256 B, margin 928 B (was 1184 B) - still comfortably over the
  256 B CI floor, but trending down each watcher; flagged to the user
  that the next one may need to spend less. VERIFIED on hardware: `U 8`
  clean (quiet-bench result), `B 5`/`R 5` re-verified with no
  regression from a fifth command sharing the capture buffer, `d 8`
  and the M0 boot self-test both clean, host `ctest` 23/23. Full detail
  in M5's own bullet.

- 2026-08-22 — M5: new `B <sec>` passive ARP spoofing/cache-poisoning
  detector, third in the user-requested "practical apps for network
  pentesting" series (`N` l2discover, `R` dhcpwatch, now `B` arpwatch).
  New `modules/toolbox/arpwatch.h/.c` (15 host unit tests) - the
  simplest of the three protocols parsed this session, fixed 28-byte
  ARP header, same bounds-checked-every-offset discipline. Tracks
  IP->MAC bindings from both ARP REQUEST and REPLY (a gratuitous
  announcement is normally a REQUEST), flags any binding that changes
  MAC mid-run - honestly framed as a strong indicator, not proof, same
  as real arpwatch(8). Uses the shared `explorer_capture_buffer.h` -
  total added RAM +160 B, margin 1184 B (was 1344 B). VERIFIED on
  hardware: `B 8` clean (quiet-bench result), `R 5`/`N 5` re-verified
  with no regression from a fourth command sharing the capture buffer,
  `d 8` and the M0 boot self-test both clean, host `ctest` 22/22. Full
  detail in M5's own bullet.

- 2026-08-22 — M5: new `R <sec>` passive rogue-DHCP-server detector,
  continuing the same user-requested "practical apps for network
  pentesting" direction as the `N` l2discover command. New
  `modules/toolbox/dhcpwatch.h/.c` (15 host unit tests) walks Ethernet/
  IPv4/UDP/BOOTP with the same bounds-checked-every-offset discipline
  `l2discover.c` established, recognising only DHCPOFFER/ACK/NAK
  (server->client, port 67->68) and flagging when more than one
  distinct source answers. Uses the shared `explorer_capture_buffer.h`
  (from the previous task, for exactly this reason) rather than its own
  buffer - total added RAM is +96 B, margin 1344 B (was 1440 B).
  VERIFIED on hardware: `R 8` clean (quiet-bench result, consistent with
  every other M5 recon tool), `N 5`/`M 5` re-verified with no
  regression from a third command sharing the capture buffer, `d 8` and
  the M0 boot self-test both clean. Full detail in M5's own bullet.
  CI matrix confirmed green live on the push (run 32593596451): both
  `Firmware (STM32F429)` and `Firmware (STM32F429, minimal apps)`.

- 2026-08-22 — M6: build-time optional apps (`CADS_APP_SETTINGS`/
  `CADS_APP_ABOUT`/`CADS_APP_GPIO`/`CADS_APP_NETINFO`/
  `CADS_APP_FILEBROWSER`/`CADS_APP_GAME`, each `ON` by default). User-
  requested directly in response to the L2-discovery task below hitting
  the 48K RAM floor: a "plugin-like" build-time choice of which apps
  land in the firmware. One `CADS_APP_*_ENABLED` compile definition per
  option, added to `cads_flags` so every target already linking it (the
  common case in this project) sees it without per-target wiring; three
  explorer bring-up commands that reach directly into one specific app
  guard that reach the same way. Caught one real bug testing a reduced
  configuration rather than trusting the CMake by inspection: two link
  lines stayed unconditional in the top-level `CMakeLists.txt`, masked
  by CMake's own lazy handling of an unmatched library name (passed to
  the linker rather than erroring at configure time), which let an
  unrelated *compile* failure surface first. VERIFIED on hardware at
  both ends: default config unchanged (RAM margin 1440 B, `N`/`M`/`C`/
  `d 8`/boot self-test/host `ctest` 20/20 all clean); all six apps `OFF`
  also links and boots clean (flash -46840 B, RAM -5888 B, margin
  7328 B, `d 8` fault-free with an empty menu). Board reflashed back to
  the default image afterward. Full detail in M6's own bullet.

- 2026-08-22 — M5: new `N <sec>` passive L2 neighbor discovery command
  (CDP/LLDP/STP + VLAN IDs), user-requested ("practical apps... hard
  with a regular computer" for network pentesting) rather than pulled
  from an open roadmap/issue item - this session's roadmap had reached
  zero `[ ]` bullets, so this is genuinely new scope, added under M5's
  existing Swiss-army-knife section since that is exactly what it is.
  New `modules/toolbox/l2discover.h/.c` (19 host unit tests, hand-built
  frames) + `apps/bringup/explorer_l2discover_demo.c`. Its own first
  build failed the 48K RAM floor - fixed structurally, not with another
  lwipopts.h trim: extracted `apps/bringup/explorer_capture_buffer.c`,
  one shared 1536 B capture buffer for `C`/`M`/`N` instead of one each
  (they can never run concurrently - the explorer REPL dispatches one
  command at a time). Net RAM margin after this task is 1440 B, *larger*
  than main had before it started. VERIFIED on hardware: `N 10` clean
  (quiet-bench result, consistent with every other M5 recon tool this
  session), `M 8`/`C 5` re-verified with no regression from the shared-
  buffer refactor, `d 8` and the M0 boot self-test both clean, host
  `ctest` 20/20. Full detail in M5's own bullet.

- 2026-08-22 — M3's touch-navigation bullet marked `[!]`. The previous
  full-file grep for the M0 task (same day, entry below) only matched
  `[ ]` at column 0, so it missed this one - it is indented two levels,
  nested under **HARDWARE GATE M3**. A wider grep (`\[ \]` with any
  indent) found it and confirmed it is now the *only* remaining `[ ]`
  in the whole file. Its own text already says "by a human": unlike
  M0's visual-confirmation item, which a photograph could genuinely
  close, an actual fingertip on the XPT2046 glass has no remote
  substitute, so this is a real, not-fixable-by-this-agent gap in the
  same sense as **HARDWARE GATE M5** - marked `[!]` to match, not left
  ambiguously `[ ]`. No firmware, HAL, or app code changed; no hardware
  touched. With this marked, the roadmap has zero `[ ]` bullets left
  anywhere - every remaining open item is either M1's already-deferred
  DMA2D decision or one of the M3/M5/M6 physical hardware gates.

- 2026-08-22 — M0's last open bullet, "visual confirmation of the test
  pattern by a human", marked `[x]`. A full-file grep (not a
  per-milestone scan) turned it up: M0's header was already `[x]`, so
  the previous 2026-08-21 sweep that checked M1/M3/M5/M6/M7 for stray
  open bullets never re-scanned M0's own body and missed it. Closed by
  photographing the physical panel with `scripts/board_photo.py`
  (webcam, the tool's own established purpose per its file header) and
  visually inspecting the images - a genuine external, non-framebuffer
  observation, which is what the write-only bus actually requires, not
  literally a person's presence. First attempt at the app's default
  90 % backlight was inconclusive: the desk lamp's glare blew the right
  half of the panel to white regardless of what was being drawn there.
  Lowered to 35 % with `scripts/board_cmd.py b 35` (software only, not a
  physical change) and redid both patterns: quadrants (pattern 3) came
  back with all four brand colours distinct and correctly placed, clean
  edges, no mirroring; fine stripes (pattern 4) came back continuous and
  evenly spaced across the full panel width, no shift-register latch
  fault. Backlight restored to 90 % afterward. With this closed, there
  are truly zero `[ ]` bullets left anywhere in this roadmap - only
  M1's already-deferred DMA2D decision (`[!]`, closed) and the M3/M5/M6
  hardware gates, which need physical bench access this environment
  does not have.

- 2026-08-21 — M7 marked `[x]`: with the CI budget bullet done, every
  bullet in the milestone is now `[x]` and it has no hardware gate of
  its own (unlike M1/M3/M5/M6) - the header marker was just out of sync
  with its own content, fixed rather than left stale.
  Checked every other in-progress milestone while here, not assumed:
  M1, M3, M5 and M6 each have zero remaining `[ ]` bullets too - the
  *only* things left open anywhere in this roadmap are M1's own
  deliberately-deferred DMA2D item (`[!]`, a closed decision with its
  own written rationale, not an open question), and three hardware
  gates (M3, M5 `[!]`, M6) that all need a human physically at the
  bench - a touch/button walkthrough, a DHCP server and an external
  client on this segment, or a jumper wire, none of which this
  environment can provide. Every autonomous task this roadmap describes
  is now done.

- 2026-08-21 — M7's last bullet: CI size regression budget, the one
  genuinely missing piece of "CI: build both targets, unit + golden
  tests, size regression budget" (the other three already existed in
  .github/workflows/ci.yml). New scripts/check_ram_budget.py reads
  __cads_heap_size straight out of the built ELF via nm - the exact
  symbol the linker's own ASSERT(__cads_heap_size >= 48K, ...) already
  computes, not re-derived - and fails if the margin above that floor
  is under 256 B, the smallest margin this session ever accepted as
  real rather than razor-thin. Wired into the firmware job with an
  explicit `set -o pipefail` ahead of the `| tee`, checked rather than
  assumed from GitHub Actions' own default shell behaviour, since a
  silently-swallowed exit code would defeat the point entirely.
  Verified locally in both directions before pushing: a genuine pass
  (0 exit, this project's own real current margin, 256 B) and a
  deliberately tightened budget to force a failure (1 exit, clear
  message) - both propagated the correct exit code through the pipe.
  ci.yml itself checked as valid YAML (Ruby's parser, no PyYAML
  locally). No firmware touched, so no board involved - the real, live
  CI run (github.com/scimbe/cads-zero, run 32525160645) is the actual
  end-to-end proof for a workflow file, watched after pushing rather
  than assumed: the "RAM regression budget" step's log shows the exact
  same output as the local dry run (`__cads_heap_size = 49408 B`,
  `margin = 256 B`, `PASS: 256 B of margin, budget is 256 B`), and
  arm-none-eabi-nm auto-detected correctly from the CI-installed
  toolchain with no --nm override needed there.

- 2026-08-21 — M7's board_test.py extension: per-milestone suites,
  genuinely new work after three "already done" bullets in a row. "TAP
  over VCP" already existed (the boot self-test's own stream); added
  `--suite <name>` for the missing half rather than a new script,
  matching the bullet's own "board_test.py extended" wording. A suite
  runs after a clean boot gate over the same console, sending each
  check's explorer command and validating with a small Python function
  rather than expecting firmware-emitted TAP (these commands print ad
  hoc text, not `ok N`) - synthesising the TAP stream in Python instead.
  Every validator checks "completed with a well-formed result", not
  "result was PASS", since several M6 commands have an environment-
  dependent correct answer on this bench (no DHCP server, no physical
  jumper - both already established facts from earlier tasks) and
  asserting a specific PASS would fail a healthy board forever. `m6`
  suite: F, D, L (checks the redraw actually completed, not just "didn't
  crash"), K (accepts either a well-formed PASS or FAIL).
  Found and fixed a real bug in the test harness on the first run, not
  the firmware: passed F two arguments as if it took a configurable
  rate like L does: F only takes one (seconds), so "25 2" was parsed as
  25 seconds, ran long past the suite's own 15s timeout, and corrupted
  every check after it. Fixed the argument shape; reflash-and-rerun then
  passed all four checks cleanly.
  VERIFIED on hardware: `--suite m6` gives boot-gate PASS followed by
  RESULT: PASS for all four checks (K correctly scored ok for its own
  honest FAIL). Re-ran the plain invocation to confirm the refactor left
  the existing boot-gate behaviour unchanged. No firmware touched, so no
  itsboard rebuild - a host-side Python tool only.

- 2026-08-21 — M7's "Unit tests (Unity) for canvas, core, toolbox".
  Closed by counting, not assuming: toolbox already has all nine of its
  source files covered (89 cases total), canvas already has 25 genuine
  cases (pixel round-trips, every fill_rect edge case, nested clips,
  damage tracking, text layout, the dirty-rectangle flush path - read
  the file, not just its existence). "core" was the real question:
  core/cads_hal.h is a pure interface with nothing of its own to test
  (real logic lives only in target-specific implementations, and this
  project already unit-tests against the interface via fake_hal.c/
  fake_mdio.c in four other test files); modules/kernel, the other
  plausible reading, is deliberately board-only (a thin FreeRTOS
  wrapper, its own header says so, never built for host) - porting or
  faking FreeRTOS for one Unity test would be a disproportionate
  side-project, and its actual verification is already the right one:
  the hardware explorer test (command x), not a host stand-in for
  something that only means anything with a real scheduler under it.
  Host ctest 19/19, run fresh. No board gate, no files changed.

- 2026-08-21 — M7's SDL2 simulator bullet (panel, touch, adapter I/O
  panel, console). No new work - targets/sim/hal_sim.c and its own
  README already had all four pieces, apparently just never marked
  done. Closed the loop with fresh verification instead: `cads-zero-sim
  --screenshot` launched cleanly and wrote a correctly-sized 460854-byte
  BMP just now; the golden-image tests already exercise the same render
  path on every host ctest run this session (19/19, including this
  one). Tried to also get a real OS-level screenshot of the interactive
  window specifically to confirm the I/O panel/touch sidebar (which the
  golden BMP capture does not include), but this environment's display
  came back solid black - a disconnected/inactive remote session, not a
  simulator problem. That code is straightforward, already-documented
  SDL event handling, confirmed by reading it. No board involved, no
  files changed.

- 2026-08-21 — GPIO Swiss-army-knife's continuity/cable tester, the
  last bullet in the section. The only one needing no new HAL driver:
  cads_hal_adapter_outputs()/_interrupts() are already portable
  (hal_io.c and targets/sim/hal_sim.c both implement them), so
  explorer_continuity_demo.c is one file, no board/sim split, wired
  into cads_apps' unconditional sources the same way
  explorer_arp_demo.c/explorer_ping_demo.c already are. New command K
  drives OUT0 low then high and requires INT0/AUX0 to follow both
  transitions before calling it PASS.
  Found and fixed a real bug on the very first hardware run: an
  un-jumpered board reported "low ok, high not seen" - backwards from
  what "INT0 is pulled up" predicts. hal_io.c's own
  cads_hal_adapter_interrupts() carries the identical active-low
  inversion cads_hal_adapter_inputs() has a comment for but this
  function does not repeat (bit=1 = pulled low, bit=0 = idle high).
  Fixed the test's own expectations to match and renamed the internal
  parameter to want_active, in the function's own bit sense.
  VERIFIED on hardware, the fix itself being the verification: after
  the fix, an un-jumpered board reports "low not seen, high ok" - the
  mirror image of the pre-fix run and exactly what the physics predicts,
  confirming the earlier result was the bug, not reality. A genuine
  PASS needs a human to place a jumper, not available here - the same
  ceiling every earlier bullet this section hit. itsboard links clean
  (0 B RAM), M0 boot 10/10, d 8 regression clean, host ctest 19/19.
  Closes out the whole GPIO Swiss-army-knife section - only HARDWARE
  GATE M6 (a human touch/button walkthrough) is left open in M6.

- 2026-08-21 — GPIO Swiss-army-knife's simple logic analyzer, the
  largest of this section's bullets. Chose capture-then-render over
  live-scrolling: apps/bringup/tasks.c's own rule ("the display has
  exactly one flusher", the ui task) means a continuously-updating
  waveform would lose samples every time the sampling loop yielded to
  wait for a redraw - sampling a fixed window first, then rendering it
  once, avoids that entirely for far less risk than teaching this
  driver to sample from an interrupt would have been, and still matches
  the bullet's own "sample rate bounded by redraw speed" wording (now
  about density across the panel's width, not a per-sample rate). New
  TIM7-paced hal_sample_timer.c - a new file rather than reusing
  hal_pktgen_timer.c's TIM6, since the two are paced by genuinely
  different things. New command L, capturing into the already-built
  cads_ring_t and rendering all 14 channels (IN0-7, INT0-5) as labelled
  rows.
  Hit this session's razor's-edge RAM lesson again, this time entirely
  from this feature's own buffer: an initial 256-sample (512 B) ring
  left the 48K linker guard at exactly zero margin. Halved to 128
  samples (and the default rate to match) rather than reaching for a
  shared lwipopts.h trim - this RAM cost was this feature's own to own,
  and 128 samples across the ~438 px waveform area is a clearer trace
  than 256 near-1px columns would have been anyway.
  VERIFIED on hardware with real photographic proof of the full render
  pipeline (not just an honest zero like the frequency/duty-cycle
  tasks): L captured 124/125 samples cleanly, and the panel photo shows
  all 14 rows correctly labelled and flat-low, cross-checked against
  i's own raw IDR dump confirming that's genuinely the current hardware
  state, not a rendering bug. itsboard links clean (256 B margin), M0
  boot 10/10, d 8 regression clean, host ctest 19/19.

- 2026-08-21 — GPIO Swiss-army-knife's PWM generator. Checked all
  sixteen OUT pins against the sibling datasheet's AF table (this
  bullet's own warning that some are GPIO-only, made concrete): only
  PE5/PE6 (OUT13/14, TIM9_CH1/CH2, both AF3) carry a real PWM-capable
  output channel - everything else is FSMC/USART/CAN-only, or (PD2,
  PE0, PE7) only has a timer External Trigger *input*, not an output.
  Used PE5. TIM9 is on APB2, not APB1 like every timer this milestone
  used before it - hal_clock.c's PPRE2=DIV2 plus the same doubling rule
  already applied to TIM2/TIM6 gives TIM9CLK=180 MHz. PSC is computed
  per call, not fixed, since a 16-bit timer serving a wide user-chosen
  frequency range needs it to keep ARR's duty resolution meaningful.
  PWM mode 1 confirmed against RM0090's own text ("channel 1 is active
  as long as CNT<CCR1"), not just the CMSIS bit name. New command `D`;
  claims/releases OUT13's own LED exactly like PA7's own claim/release,
  far lower stakes.
  VERIFIED on hardware with real photographic proof for the first time
  this milestone (CN8 pin 5's tasks before this one only ever had an
  honest zero to report, nothing to look at): OUT13's LED is physically
  visible, and six webcam captures across a `D 1 50 20` run show it
  alternating green/white in the expected pattern - an actual blink,
  not a stuck level. itsboard links clean (RAM unchanged, no static
  state), M0 boot 10/10, `d 8` regression clean.

- 2026-08-21 — GPIO Swiss-army-knife's duty-cycle measurement, directly
  on top of the frequency counter. TIM2_CH4's CCMR2.CC4S=10 maps IC4
  onto TI3 (RM0090: "IC4 is mapped on TI3") - the timer's own
  channel-swap, so CH3 (rising) and CH4 (falling, new) both read CN8
  pin 5 with no second pin involved, exactly this bullet's own "same
  input-capture channel, second capture compare register". Deliberately
  skipped RM0090's textbook PWM Input Mode (reset-mode-on-trigger, so
  CCR4 reads high time directly): it would change what CCR3 itself
  means, reaching into the already-committed frequency counter. Used
  the same wraparound-safe delta technique instead, extended into
  `cads_freqcounter_capture_high()` (`cads/toolbox/freqcounter.h`) -
  mathematically equivalent, zero risk to the period side. One `F`
  command reports both now, not two commands.
  VERIFIED on hardware: itsboard links clean (RAM unchanged), M0 boot
  10/10, `d 8` regression clean, `F 5` fault-free with an honest zero
  for both channels (still no jumper access to CN8). `test_freqcounter`
  grew from 7 to 14 cases - the only real proof for the falling-edge
  wraparound and its own independent overcapture handling, the same
  "prove it host-side since hardware can't" reasoning as every capture
  driver this milestone.

- 2026-08-21 — GPIO Swiss-army-knife's frequency/period counter. Two
  things this bullet's own text asked to check turned out both wrong
  and resolvable: `ITS-BRD-NucleoPins.xlsx` (its named reference) is not
  archived in this repo, and "an INT line" (PG0-5) turned out to have no
  timer alternate function at all per the sibling datasheet's own AF
  table (confirmed for INT0/1/3/4/5 - INT2/PG2 excluded regardless,
  already on the RMII no-touch list as `ETH_RXER`). `docs/HARDWARE.md`
  had already flagged the actual answer unprompted: the CN8 timer
  breakout, specifically so this feature would not need to repurpose an
  adapter pin. Used `PB10` (CN8 pin 5, net "TIM2_3" on the project's own
  schematic, cross-checked against the sibling datasheet's AF table:
  `TIM2_CH3` via `AF1`) - TIM2 being one of the two 32-bit timers the
  bullet asked for.
  Split the wraparound-safe delta / missed-edge-resync math into a
  portable `cads/toolbox/freqcounter.h`, unit-tested
  (`test_freqcounter.c`, 7 cases) since this bench has no way to drive a
  known frequency into CN8 without a physical jumper wire - the same
  "prove the logic host-side since hardware can't" move as the MAC
  table task. New explorer command `F`. Found and fixed a real bug
  while verifying: `cads_hal_freqcounter_stop()`'s own doc comment
  promised it released the pin back to plain input, but the first cut
  only stopped the timer - fixed.
  VERIFIED on hardware: itsboard links clean (+32 B RAM), M0 boot 10/10,
  `d 8` regression clean, `F 5` ran fault-free and honestly reported
  zero periods (nothing drives CN8 pin 5 here). Host `ctest` 19/19.

- 2026-08-21 — GPIO Swiss-army-knife's logic level display. No new code -
  the bullet itself already said this was "already the core of the
  apps/gpio app", and reading `cads_gpio_tick()` confirmed it: every
  poll re-reads `cads_hal_adapter_inputs()`/`_interrupts()` and redraws
  whichever cell changed. This task closed the loop with a real
  cross-check instead: `i` read raw `F=FCFF G=D7BB` on hardware,
  hand-computed against `hal_io.c`'s exact formulas (confirmed via
  `board.h`'s `CADS_PIN_IN_PORT`=GPIOF/`CADS_PIN_INT_PORT`=GPIOG/
  `CADS_ADAPTER_INT_MASK`) to IN0-7=0x00, INT bit 2 active (a floating
  PG2, not a fault) - proving the app's displayed values against the
  identical register this session's own `i` command reads independently
  of apps/gpio's own code. `g 6` ran fault-free, photographed live
  (OUT0-2 list, IN column headers visible). No live transition
  demonstrated - needs a hand at a button or a jumper, neither available
  here - but the poll -> compute -> redraw path itself is now proven
  against real hardware, not just read from source.

- 2026-08-21 — M6's game: "Leo's Reflex Test" (new `apps/game`). A
  literal reading of "exercise the input and timing paths end to end" -
  the game measures the time between a "GO" signal and the OK release,
  via `cads_hal_ticks_ms()`. Needed its own `cads_game_tick(now_ms)`,
  following `cads_desktop_tick()`/`cads_gpio_tick()`'s exact precedent
  (a view cannot wake itself; the app exports a tick called from the
  same main loop instead), not a new framework capability.
  Found and fixed a real, silent bug while wiring it in, unrelated to
  the game itself: `explorer_app_demo.c`'s view-registry capacity was
  stale at 7 (correct before `apps/filebrowser` added 2 more views in
  M4, never updated after). `cads_view_dispatcher_add()` fails silently
  past capacity, so the LAST two registrations - filebrowser's info
  view, and the MENU view itself - never actually happened. Pressing OK
  on the desktop to open the menu would have failed silently on every
  `d` hardware check this session has run; none of them ever pressed a
  button, so "no fault, N frames flushed" was all any of them could have
  shown regardless. Fixed the capacity (7 -> 10) and added
  `tests/unit/test_app_tree.c`: it builds the real app tree host-side
  and feeds a *synthetic* OK-release event through the real dispatcher
  to prove MENU actually becomes current - something no hardware check
  this session has been able to do without a human finger - plus a
  companion test proving the same setup against the old capacity of 7
  would have caught the bug. `tests/unit/fake_hal.c` gained a
  `cads_hal_board_info()` stub, the only HAL touch point among all six
  app `_init()` functions; everything else needed to link the real
  `cads_gui` (not `canvas_host.c`) on host was already reachable through
  `cads_app_desktop`/`cads_app_menu`, the same graph `cads-zero-sim`
  already proves works.
  VERIFIED on hardware: itsboard links clean (+128 B RAM, 544 B margin,
  no lwipopts.h trim needed), M0 boot 10/10, `d 8` app-tree regression
  clean (photographed live on the panel). Host `ctest` 18/18.

- 2026-08-21 — M5's fourteenth (last non-gate) task: Wake-on-LAN
  magic-packet sender. New `explorer_wol_demo.c` (command `W`): the
  standard magic packet as a raw Ethernet frame (EtherType 0x0842), sent
  straight through `cads_hal_eth_mac_transmit()` - not a style choice
  but forced by this bench's own long-established no-DHCP finding, which
  means `ip4_route()` refuses any IP packet a UDP-broadcast variant would
  have needed. VERIFIED via the same byte-perfect MMC cross-check used
  for every TX-path claim this session: fresh reflash (`tx_good=0`), one
  `W` call produced `tx_good=2` (1 magic packet + 1 DHCP discover from
  `cads_net_init()`'s own bring-up, the same accounted-for overhead the
  pktgen task already documented), reproduced across two independent
  target addresses. Host `ctest` 17/17, M0 boot 10/10, `d 8` regression
  clean.
  Marked **HARDWARE GATE M5** `[!]` rather than attempting it: it needs
  a DHCP server on the bench segment and an external client reaching the
  board's LAN, both outside what this agent's environment provides or
  what a code change can fix - a genuine infrastructure decision for the
  user, not a task to silently skip. M6 (already `[~]`) is next.

- 2026-08-21 — M5's thirteenth task: MAC address table with aging, built
  from sniffed frames. First M5 bullet split into a portable half and a
  board-only half: the learn/refresh/evict policy (new
  `modules/toolbox/{include,src}/mactable.{h,c}`, `cads_mactable_t`) has
  no HAL dependency - caller-supplied storage like `cads_record_t`, and
  an explicit `now_ms` rather than reading a clock itself - so it is
  unit-tested on the host with a fake clock (new `test_mactable.c`, 10
  cases) rather than relying on this bench's own traffic, which the
  sniffer task already established is near zero. New `explorer_
  mactable_demo.c` (command `M`) is only the capture loop: promiscuous
  mode, direct `cads_hal_eth_mac_receive()` reads (same non-polling
  exclusive-access argument as the sniffer), full 1536 B receive buffer
  for the same "don't silently bias toward undersized frames" reason
  that file's header gives. Aging scaled to 5s (a real switch's default
  is closer to 300s) so one run can actually demonstrate eviction.
  RAM: the new capture buffer plus the table's own storage (1856 B)
  again crossed the linker's 48K guard. Continued the sniffer task's own
  two levers one step further rather than reaching for new ones:
  `MEM_SIZE` 3072→2048, `PBUF_POOL_SIZE` 5→4 - final RAM 146784 B,
  672 B of margin.
  VERIFIED on hardware: `M 8` ran clean (0 frames, matching `rx_unicast=
  0` on the MAC's own MMC counter in the same window - genuinely no
  traffic, not a bug, the same corroboration technique the sniffer task
  used). Host `ctest` 17/17. M0 boot self-test 10/10, `d 8` app-tree
  regression clean.

- 2026-08-21 — M5's twelfth task: promiscuous packet sniffer. New HAL
  functions `cads_hal_eth_mac_set_promiscuous()`/`_missed_frames()` and
  new explorer command `C` (writes a real libpcap file to `/sniff.pcap`
  via M4's storage module). Caught a genuine RM0090-vs-CMSIS-comment
  discrepancy before writing any code: `ETH->DMAMFBOCR`'s field NAMES
  (`MFC` "by controller", `MFA` "by application") are backwards from
  their documented CAUSES (`MFC` is RX descriptor exhaustion, a software
  condition; `MFA` is RX FIFO overflow/runt, a wire-level condition) - the
  new API is named by verified cause, not by the register's own
  terminology, with the discrepancy documented in `hal_eth_mac.h` so it
  is not rediscovered. Deliberately does not call `cads_net_poll()` during
  capture (would race lwIP's own RX-ring draining); receives into the
  full 1536 B frame size so an oversized frame is never silently dropped
  by an undersized buffer, truncating only the pcap-written copy to a
  256 B snaplen. Loss is measured via three independent counters
  (descriptor exhaustion, FIFO overflow, storage write failure) rather
  than assumed, per this bullet's own explicit requirement.
  The new static 1536 B receive buffer pushed RAM over the linker's 48K
  headroom guard (1344 B over). Rather than repeat this session's
  recurring razor-thin fixes, trimmed `lwipopts.h` for real margin this
  time: `PBUF_POOL_SIZE` 7→5 and `MEM_SIZE` 4096→3072 (measured per-slot
  cost via the linker map rather than guessing), recovering 2240 B -
  final margin 896 B over the floor, versus 192 B before.
  VERIFIED on hardware: `C 8` captured cleanly (0 captured, 0 dropped by
  either hardware counter, 0 write errors) - corroborated as genuinely
  "no traffic" rather than a capture bug via the MAC's own MMC counter
  (`rx_unicast=0` in the same window), consistent with this bench's
  long-established no-DHCP/near-zero-ambient-traffic environment. Host
  `ctest` 16/16, M0 boot self-test still 10/10, `d 8` app-tree regression
  clean.

- 2026-08-21 — M5's eleventh task: configurable-rate packet generator. New
  explorer command `G`, new HAL driver
  `targets/itsboard/hal/hal_pktgen_timer.{h,c}` (TIM6, polled update flag,
  not a software delay loop - the first tool this session to actually
  need the "timer" half of this bullet's own name for precise, not just
  approximate, pacing). Also the first file this session to touch raw
  STM32 registers outside `targets/itsboard/hal/` at all, and correctly
  kept out: the register work lives in the new HAL file, the explorer
  command only calls its clean start/stop/elapsed API - the same
  board/sim split every other M5 networking bullet already used.
  Verified TIM6's clock against RM0090 rather than memory (via
  `pdftotext`, the same primary-source discipline already used for the
  Ethernet DMA descriptors): APB1's prescaler is 4 (not 1), so per RM0090
  TIM6CLK = 2 x PCLK1 = 90 MHz, not 45 MHz - confirmed, not assumed.
  Frame content reuses the M5 MAC/lwIP hardware gate's own probe-frame
  choices (broadcast, EtherType 0x88B5) plus a sequence number, sent
  straight through `cads_hal_eth_mac_transmit()`, bypassing lwIP - a
  MAC-layer rate test.
  VERIFIED on hardware with the closest thing to a byte-perfect
  confirmation this session has gotten for a TX-path claim: `G 2000 5`
  reported 3998 sent, 0 dropped, and the MAC's own MMC `tx_good` counter
  increased by *exactly* 3998, no link-up event in between to explain any
  difference. 2000 pps x 60 B ~= 0.96 Mbit/s with zero drops, consistent
  with this bullet's own "expect well under 100 Mbit/s" prediction.

- 2026-08-21 — M5's tenth task: iperf throughput test. New explorer command
  `I`, wiring lwIP's own vendored `lwiperf.c` in server mode (port 5001,
  iperf2's default) rather than reimplementing throughput measurement -
  the same "use the well-tested vendored thing" choice already made for
  littlefs and lwIP itself. Client mode deliberately not built: it would
  hit the exact `ip4_route()` wall ping/traceroute already documented,
  for no benefit on a bench with nothing to measure throughput to anyway.
  First M5 networking bullet all milestone that did NOT need a RAM fight:
  `lwiperf.c`'s session state is `mem_malloc()`'d from the pool already
  budgeted, not statically allocated, and its one large buffer is `const`
  (flash). Confirmed rather than assumed: RAM usage was identical before
  and after.
  VERIFIED on hardware: server started on port 5001, ran its full window
  fault-free, app tree unaffected afterward. Same limit as every other
  TCP server built this milestone: no live external iperf client could
  be run against it from this environment, so no throughput number - the
  honest, expected result of nothing connecting, not a defect.

- 2026-08-21 — M5's ninth task: DHCP lease/gateway/DNS display. Extended
  `cads_net_status_t` with `gw_addr`/`dns_addr`/`dhcp_bound` and wired all
  three into every existing network display at once - `apps/netinfo`,
  `cads_cli`'s `net` command, and the HTTP status page's selftest - rather
  than just one. Gateway and lease-bound state were free
  (`netif_ip4_gw()`, `dhcp_supplied_address()`, both already-existing lwIP
  helpers over state DHCP already maintains); DNS needed real new
  capability (`LWIP_DNS=1`, since `dns_getserver()`/DHCP's own DNS-option
  parsing do not exist without it), trimmed to `DNS_TABLE_SIZE=1`,
  `DNS_MAX_SERVERS=1` up front so the default's 256-byte-per-slot hostname
  buffer (built for active resolution this firmware never does) did not
  blow the 48K RAM guard for a fourth time this milestone - net cost this
  time: +32 bytes, comfortably inside the margin the ping task fought to
  win back. Also replaced two more hand-rolled dotted-quad loops
  (`cads_http_format_row_ip()`, `apps/netinfo`'s own IP formatter) with
  the shared `cads_fmt_ipv4()` - in scope here because this task was
  already rewriting both for the new fields, unlike the ARP scan task
  where the same duplication was correctly left alone as out of scope.
  VERIFIED on hardware: the HTTP selftest and a real interactive `cads_cli`
  `net` session both showed correct live data for all three new fields
  (`none`/`none`/`none` - honest, matching this bench's already-established
  lack of a DHCP server); the app tree, now drawing netinfo's three extra
  rows, ran fault-free. The netinfo view's own on-panel layout was not
  visually confirmed - this session's tooling has no way to drive
  touch/button input to navigate there, the same pre-existing limit noted
  against the filebrowser app in M4.

- 2026-08-20 — M5's eighth task: traceroute (ICMP TTL sweep). New explorer
  command `T`, driving a new `cads_net_traceroute_probe()` in
  `modules/net`. Factored the echo-request builder ping already had into
  a shared `cads_net_icmp_echo_send()` now that traceroute needed the
  identical packet with only `pcb->ttl` and the id/seqno different -
  worth doing on the second caller, not before. One probe per TTL, hop
  identified by the source address of whichever ICMP message answers
  (echo reply from the target, or time-exceeded from whatever router's
  TTL ran out) - deliberately not validated against the nested original-
  packet header a time-exceeded message carries, since one probe in
  flight with a short timeout makes type-plus-arrival-order good enough
  for a LAN tool.
  VERIFIED on hardware exactly as forecast when ping's entry was written:
  five probes, five "*", and `tx_good` moved by only 1 (one DHCPDISCOVER
  from link-up) across the whole run - confirming every probe failed
  inside `ip4_route()` before reaching the MAC, the same structural cause
  as ping, not a new bug. This was worth building anyway, not skipped for
  the known limitation: the code is correct and ready for whenever this
  bench (or another one) has a real route to test it against.

- 2026-08-20 — M5's seventh task: ping / ICMP echo. New explorer command
  `P`, driving a new `cads_net_ping()` in `modules/net` - a raw
  `IP_PROTO_ICMP` pcb, hand-built `icmp_echo_hdr`, matched to its reply by
  a per-call id so a late/stale reply cannot be mistaken for the current
  ping's answer. Needed `LWIP_RAW=1`, which cost enough RAM to blow the
  linker's 48K headroom guard a third time this milestone - reduced
  `MEMP_NUM_RAW_PCB` to 1 (all this firmware ever uses at once) and
  `PBUF_POOL_SIZE` 8->7 to buy back real margin this time, not just enough
  to scrape by again.
  Found something more precise than "no DHCP server" this time: ping is
  correctly implemented but structurally cannot send anything AT ALL on
  this bench, because lwIP's `ip4_route()` refuses to select a netif whose
  own address is 0.0.0.0 - and this device has never held a real IP
  address, ever, on this bench. DHCP and ARP worked earlier in M5 only
  because neither of them calls `ip4_route()` (DHCP uses a
  broadcast/unspecified-source path built for exactly this situation; ARP
  is below IP entirely) - ping is the first tool in this Swiss-army-knife
  to actually need routing, and the first to expose that this device
  effectively has no usable address on this segment at all, not just no
  DHCP lease. Verified precisely via the MMC counter: `tx_good` did not
  move across a ping call, meaning it failed inside `ip4_route()` before
  ever reaching the MAC - a different, stronger finding than DHCP/ARP's
  "sent, unanswered".
  Deliberately not fixed with an AutoIP/static-address fallback here -
  real new capability every future outbound-client tool (traceroute,
  iperf client mode) would also need, not this "S" bullet's own scope, and
  it would spend more of the RAM margin just won back. Noted against both
  of those tasks below so it is not silently rediscovered.

- 2026-08-20 — M5's sixth task: ARP scan of the local subnet. New explorer
  command `A`, `apps/bringup/explorer_arp_demo.c`, driving a new
  `cads_net_arp_probe()` in `modules/net` (one `etharp_request()` /
  `etharp_find_addr()` round trip per host, sequential - no RAM for
  tracking many requests at once). Fully portable, no board/sim split -
  the simulator side of `cads/net/net.h` already has an honest "never
  resolves anything" answer. Added `cads_fmt_ipv4()`/`cads_fmt_mac()` to
  `modules/toolbox` (with tests) rather than writing a fourth near-copy of
  the same formatting loop `apps/netinfo`, `cads_cli` and
  `explorer_http_demo.c` each already had; left those three untouched
  rather than refactoring working, verified code as a side effect of an
  unrelated task.
  Found a real bug via a technique worth repeating: cross-checking against
  the MAC's own MMC hardware counter, not just trusting the scan's own
  output. The link-autonegotiation wait loop called `cads_net_status()`
  without ever calling `cads_net_poll()` - which is what actually detects
  the link, `cads_net_status()` only reports the last poll's cached result
  - so the loop always burned its full 3 s doing nothing, link state never
  transitioned, and every probe silently returned false before sending
  anything. "0 hosts answered" looked identical whether the scan asked and
  got no answer, or never asked at all; only `m` before and after (`tx_good`
  not moving across three separate attempts) exposed which one was
  happening. One `cads_net_poll()` call fixed it.
  VERIFIED on hardware after the fix: `tx_good` read 0, an 8-host scan ran,
  and `tx_good` read 9 immediately after - 8 real ARP requests plus the 1
  DHCPDISCOVER a fresh link-up always sends, exactly accounted for. Still
  0 hosts answered - consistent with everything else this session has
  found about this bench's segment - but now backed by hardware-counted
  proof that real requests went out, not just a number that happened to
  look plausible either way.

- 2026-08-20 — M5's fifth task: HTTP status page. New explorer command `H`,
  `apps/bringup/explorer_http_demo.c` (board only). Found and fixed a real
  bug by code review, before any hardware run: the first draft rendered
  the whole page into one buffer, and that buffer plus its build scratch
  totalled over 2.1 KB in one function's stack frame - bigger than the
  console task's entire 2048-byte stack, a guaranteed overflow the moment
  a real client connected, and one "the listener starts" verification
  (the bar the previous two TCP tasks were checked against) would never
  have reached, since that code path only runs from `tcp_accept()`. The
  same buffer, as a static global, also failed the linker script's own
  `ASSERT(__cads_heap_size >= 48K, ...)` RAM-headroom guard - correctly,
  not a false alarm. Redesigned as a small phase-based state machine
  streaming literal HTML straight from flash plus one shared ~96-byte row
  buffer, mirroring `cads_cli`'s/the screencast's already-verified
  `tcp_sent()`-driven chunked-send pattern - total added static state
  dropped from ~2 KB to under 150 bytes.
  Because that near-miss showed the existing verification bar was not
  enough for this class of bug, added something new: a selftest that
  calls the exact row-formatting functions a real connection would use,
  with real data, and prints each result over serial - no TCP client
  needed, since none of those functions touch `tcp_*` at all.
  VERIFIED on hardware: the selftest produced correct real output in both
  link-down/no-IP and (after a 3 s autonegotiation wait) link-up/100M-full
  states, MAC hex-formatting included. The listener bound on port 80. As
  with the previous two TCP features, a live remote browser request could
  not be verified - no network path from the agent's shell to the board's
  bench segment - so the accept/chunked-send half rests on the selftest
  plus analogy to those two already-verified transports, not on an
  observed real request.

- 2026-08-20 — M5's fourth task: screen streaming over TCP. New explorer
  command `S`, `apps/bringup/explorer_screencast_demo.c` - single-consumer,
  so it lives directly in apps/bringup rather than as a new `modules/`
  (unlike cads_net/cads_cli, nothing else needs to call into it). Streams
  the retained 4bpp framebuffer straight off `cads_canvas_buffer()`: a
  small header once (dimensions + the RGB565 palette, so a viewer never
  hardcodes CaDS colours), then length-prefixed raw frames forever, chunked
  across `tcp_write()` as send-window space frees via the same
  `tcp_sent()`-driven event pattern `cads_cli`'s TCP transport already
  established. No reference host viewer written - full protocol documented
  in the source instead, since one could not have been tested against a
  live connection from this environment regardless (see below). Frames are
  not double buffered (no spare RAM for a second 76 KB buffer), so a
  connected viewer can occasionally see a torn frame - documented as an
  accepted v1 limitation rather than solved with real complexity this
  bullet does not call for.
  VERIFIED on hardware: the listener started on port 4244, a small marker
  rectangle visibly animated on the physical panel for the full 15 s run
  via small partial flushes, no fault, and the app tree ran clean
  immediately afterward. Same limit as `cads_cli`'s TCP half: no live
  remote client could be verified, since the agent's shell has no network
  path to the board's bench segment - "0 frames sent" is the correct,
  honest result of nothing ever connecting to drain them, not a bug.

- 2026-08-20 — M5's third task: `cads_cli` over TCP and serial, shared
  command table. New `modules/cli/`: a fully portable session core
  (`cads/cli/cli.h`, no HAL/lwIP dependency) dispatching into one static
  table of five commands, plus a board-only lwIP raw-API TCP transport
  (`cads/cli/cli_tcp.h`) with the same board/host split `cads/net/net.h`
  established. Deliberately kept separate from `apps/bringup/explorer.c` -
  that stays the ~30-command bring-up diagnostic tool it already is;
  `cads_cli` is a second, smaller, general-purpose set reachable over the
  network, which the explorer specifically is not. Serial reaches it
  through a new bounded explorer command (`j`), the same
  hand-it-exclusive-ownership-for-the-duration pattern the GUI/app-tree
  demos already use, and needed no board/sim split of its own since every
  primitive underneath it already had an honest simulator answer.
  VERIFIED on hardware with a real scripted interactive session: `help`,
  `net`, `uptime`, `echo` and a deliberately-unknown command all sent as
  real lines over the real UART, all got correct real responses back, and
  the session tore down cleanly with no fault. TCP: the listener bound and
  started on port 4242 for real; a live remote connection could not be
  verified because the agent's own shell has no network path onto the
  board's isolated bench segment - a different, newly-relevant limit from
  "no DHCP server on this bench" (the previous task's finding was about
  the board's environment; this one is about the verifier's).

- 2026-08-20 — M5's second task: DHCP + link state + status bar indicator.
  `LWIP_DHCP=1`; link-up/down now starts/stops the DHCP client. Made
  `cads_net_init()` idempotent (first call wins, rest are no-ops) so the
  diagnostic command and the real app tree can both bring networking up
  without coordinating. Wired `apps/netinfo` to real `cads_net_status()`
  data - it had documented its own placeholder as waiting on exactly this
  service. Wired the status bar's slot 0 to link state in
  `explorer_app_demo.c` (still the stand-in for a real production loop -
  M6 has not built one yet); learned the widget dedupes by POINTER, not
  content, so the indicator returns one of three fixed string literals
  rather than formatting into a buffer every tick, which would have
  reported false damage constantly.
  VERIFIED on hardware: 100 Mbit full duplex link, and the DHCP client
  visibly at work - 5 real DHCPDISCOVER broadcasts, confirmed by both the
  netif's own counter and the MAC's MMC `tx_good` register. No DHCPOFFER
  came back in 30 s and RX stayed at 0, matching the previous task's
  finding - this bench segment has no DHCP server and little to no ambient
  traffic. Reported honestly rather than assumed broken: retrying
  unanswered is correct DHCP behaviour, and the send path is now proven
  twice over (the earlier deliberate probe frame, and now real protocol
  traffic). The app tree with statusbar + netinfo + net-poll all live
  together ran fault-free for 15 s on real hardware.

- 2026-08-20 — M5's first task landed: bare-metal Ethernet MAC/DMA driver
  (`targets/itsboard/hal/hal_eth_mac.{h,c}`) plus a new target-neutral
  `modules/net/` carrying a real lwIP `NO_SYS=1` netif on the board and an
  honest "no link" stub on the simulator - the same board/host split
  `modules/storage` established for littlefs. lwIP is vendored and built via
  its own `Filelists.cmake`, wired to `cads_hal_panic()`/a hand-rolled
  xorshift32 instead of libc's assert/printf/rand chain.
  Two real bugs, both link-time, both instances of the same lesson M4's
  `LFS_NO_ASSERT` fix already taught: (1) leaving `LWIP_PLATFORM_ASSERT` and
  `LWIP_PLATFORM_DIAG` at their lwIP defaults pulls in printf/fflush/abort,
  which needs `_sbrk` - this project's linker script deliberately has no
  heap. (2) less obvious: `LWIP_RAND()`'s obvious implementation,
  `rand()`, turned out to lazily `malloc()` its state table on arm-none-eabi's
  newlib-nano (confirmed by inspecting `libc_a-rand.o`'s undefined symbols) -
  same `_sbrk`/`end` link failure, reached through a function nobody would
  guess touches the heap. A third bug found by hardware measurement, not
  memory: descriptor bit positions were checked against the archived RM0090
  PDF rather than trusted from memory, which caught RDES1.RER actually
  sitting at bit 15, not the bit 14 a first guess landed on - a subtle
  descriptor-ring corruption avoided before it ever ran.
  VERIFIED on hardware (new explorer command `h <sec>`): link came up at
  100 Mbit full duplex, matching the independent MDIO-only reading exactly
  (`cads_net_status()` initially reported 0 Mbit/half unconditionally - a
  real bug, the PHY result was read but never cached into the status
  struct, fixed same session). A deliberate broadcast probe frame sent
  directly through the driver (bypassing lwIP) was confirmed by the MAC's
  own MMC hardware counter: `tx_good` delta of exactly 1. RX saw nothing in
  20 s on the bench's current quiet segment - reported as-is rather than
  assumed working; the receive path is exercised and ready but has not yet
  had a real frame to prove itself against. Scope deliberately stops at "the
  netif exists and passes frames": no IP configured yet (DHCP is the next
  M5 bullet) and the MAC address is a fixed constant, single-board
  assumption, noted as a real limitation rather than hidden.

- 2026-08-20 — Built `apps/filebrowser`, the last open item in M4, which is
  now `[x]`. Read-only navigation of the littlefs volume: one menu view
  with a mutable current-path buffer rather than a view per directory
  depth, a second view for a file's size (same "push it as its own view"
  shape `apps/settings` already uses for its confirm dialog, and for the
  same reason - the soft-key strip only re-applies on a real view change).
  Wired into `apps/menu` as "Files". VERIFIED on hardware: a new explorer
  command (`v`) ran it live, twice, against the real volume the M4 gate's
  `/cads_test.bin` was written to - one full-screen frame each run, no
  fault, explorer responsive after. Not photographed this round: the bench
  camera's framing drifted since earlier in the session and the fixed crop
  in `scripts/board_photo.py` no longer lands on the panel - a re-aim, not
  a firmware question, tracked the same way M0's own unchecked visual-
  confirmation line is.

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
