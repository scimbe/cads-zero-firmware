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
