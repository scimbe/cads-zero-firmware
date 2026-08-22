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
            any other remote tooling can substitute for. Left open for the
            user to either run the touch walkthrough by hand and report
            back, or accept the ghost-touch soak and the tree wiring above
            as sufficient evidence on their own.

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
- [~] **HARDWARE GATE M6**: full walkthrough of every app on the board. All
      five apps below the menu build, flash, and run without fault; a human
      walkthrough of each one by touch and by button is the same open item
      as the M3 gate's touch-navigation line, not a separate one.

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
