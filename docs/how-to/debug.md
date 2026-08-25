# Debug on the board

Three tools do the work here, and they hand off to each other:

- **The fault console** — when the firmware faults, it prints a labelled
  register dump over the serial port and *halts*, keeping the evidence live.
- **`st-util` + GDB** — attach to that halt (or any running board) and inspect
  memory, registers, and the clock tree directly.
- **The bring-up explorer** — a live single-letter command console for asking
  the board's own subsystems what state they are in, before a fault ever
  happens. Every command is catalogued in
  [the explorer console reference](../reference/explorer-console.md); this page
  is about *which one to reach for when* and how it fits a debugging session.

Everything that touches the probe or the port runs under a timeout, and a stale
GDB server is the classic way to make the next step fail confusingly — both are
[rules, not suggestions](../SAFETY.md#7-test-and-debugging-conduct).

## Attach GDB

```bash
st-util --serial 066FFF565282494867161033 -p 4242 &
arm-none-eabi-gdb -q build/itsboard/cads-zero.elf \
    -ex "target extended-remote :4242"
```

Always clean up afterwards — a stale server holds the probe and the next
`flash.sh` fails in a way that looks like a hardware problem:

```bash
pkill -f st-util
```

### Where am I?

```
(gdb) monitor reset halt
(gdb) break cads_bringup_run
(gdb) continue
(gdb) bt
```

Attaching resets the target, so a backtrace taken immediately after connecting
shows the board a few milliseconds into boot, not where it was when you decided
to look. This is easy to misread as a hang.

### Time is frozen while halted

`DWT->CYCCNT` does not advance when the core is halted, so reading it twice from
GDB always gives the same value. That is not evidence the counter is broken.

## When the firmware faults, read the console first

Before you attach anything, look at the serial console. A memory, bus, or usage
fault does not silently reset this firmware — it prints. The four handlers in
`targets/itsboard/startup/fault_handlers.c` are strong definitions that override
the generated vector table's weak `Default_Handler` aliases, and each one dumps
the exception-stacked frame and then stops:

```
*** CaDS FAULT: UsageFault ***
R0  = 0x........
R1  = 0x........
R2  = 0x........
R3  = 0x........
R12 = 0x........
LR  = 0x........
PC  = 0x........      <- the faulting instruction
PSR = 0x........
CFSR = 0x00010000
HFSR = 0x00000000
```

Three things about this dump are deliberate and worth knowing before you use it:

- **The name is already narrowed for you.** `cads_fault_init()` (called from
  `hal_init.c`, right after the console comes up) enables the MemManage,
  BusFault, and UsageFault handlers in `SCB->SHCSR`. Without that step every
  fault still gets caught, but only as an undifferentiated `HardFault` — the
  reset default — and you would be decoding `HFSR` by hand to learn what
  actually happened. With it, the banner names the real fault directly.
- **`PC` is the instruction that faulted**, recovered from whichever stack was
  active — MSP for a fault in an exception or before the scheduler starts, PSP
  for a fault inside a task. The handler is a `naked` trampoline that reads
  `EXC_RETURN` out of `LR` to pick the right stack pointer *before* any C
  prologue can disturb it; that is why the frame is trustworthy rather than the
  handler's own. The full reasoning is commented at length in the source and is
  worth reading once.
- **It halted; it did not reset.** The dump ends in `bkpt #0` followed by an
  infinite loop, on the same halt-don't-reset principle as `Default_Handler`: a
  reset would throw away the only copy of the evidence. So the board is now
  sitting halted with the fault still live in memory — which is exactly the
  state you want GDB to attach to.

With a debugger already attached, the `bkpt` traps to it. On an *untethered*
board the `bkpt` escalates to a HardFault, so the same fault presents as a
lock-up with the red LED lit — the intended, safe failure mode, not a second
bug.

### Turn the dump into a source location

The dump gives raw addresses; the ELF turns them back into code. `PC` is the one
you almost always want:

```bash
arm-none-eabi-addr2line -e build/itsboard/cads-zero.elf 0x<PC>
```

or, from inside the GDB session already attached to the halt:

```
(gdb) info line *0x<PC>
(gdb) list *0x<PC>
```

`cads-zero.map` from the same build resolves an address to the section and
symbol it landed in, which is how a fault whose `PC` points *inside* a specific
routine gets identified — a `.ramfunc` bus fault was tracked down exactly this
way (see the storage notes in `docs/ROADMAP.md`).

### Decode `CFSR` by hand

The dump prints `CFSR` as one 32-bit word because a reader with PM0214 open can
shift it apart faster than four extra lines of firmware could. It packs three
sub-registers (PM0214 §4.4.7–4.4.9):

| Bits | Sub-register | Covers |
|---|---|---|
| 7:0 | MMFSR | Memory-management faults (MPU, execute-never) |
| 15:8 | BFSR | Bus faults (bad address, imprecise write) |
| 31:16 | UFSR | Usage faults (undefined instruction, unaligned, divide-by-zero) |

`MMFAR` and `BFAR` — the faulting *address* for a memory or bus fault — are only
printed when their validity bits in `CFSR` say they are meaningful, so their
absence from a dump is information too: the fault was not one that latches an
address.

## Reproduce a fault on demand

You do not have to wait for a real fault to exercise all of the above. The
explorer has one destructive command that exists solely to prove the fault path
works end to end:

```
z FAULT
```

It executes `udf #0`, a deliberately-undefined instruction. It **halts the
firmware for good** — a reset or reflash is the only way back — which is why it
demands the literal argument `FAULT` and refuses a bare `z` with an explanation
rather than tripping on a fat-fingered keystroke.

Run against real hardware it produced a clean, verifiable signature:
`UsageFault`, `CFSR = 0x00010000` (bit 16, `UNDEFINSTR` — exactly what `udf`
should set and nothing else), `HFSR = 0x00000000` (proving it was handled as a
UsageFault *directly*, not escalated to HardFault, which is the on-hardware
confirmation that the `SHCSR` enable in `cads_fault_init()` is actually live),
and `MMFAR`/`BFAR` correctly absent. The board then went silent — a clean halt,
not a crash loop. That is the reference reading; a dump that differs from it is
telling you something.

The register values in the sample dump above (`R0`…`PC`) are shown as `0x……`
on purpose: they identify the faulting instruction and its operands and differ
every run. `CFSR`/`HFSR` are the diagnostic payload, and those are the values
that were verified.

## The explorer console as a live probe

Most of this board cannot report its own state without a driver you already
trust — the display bus is write-only, the GPIO banks and the Ethernet PHY have
no back channel to a human. The bring-up explorer is that channel. When a
symptom shows up, it is usually faster to interrogate the subsystem live than to
attach GDB. The mapping from symptom to command:

| Symptom | Reach for | Why this one |
|---|---|---|
| A button does nothing, or the wrong thing | `w <sec>`, then `i` | `w` watches every port's input register for changes — press the button and see which pin moves. `i` dumps all of them once for a static baseline. |
| Buttons map to the wrong labels | `s <sec>` | Streams the debounced `S0..S7` state with key names, so you confirm the mapping without a rebuild. |
| Touch registers nothing at all | `Q` (board-only), then `q` | `Q` prints the *raw* XPT2046 ADC counts, ignoring the pressed/IRQ logic entirely — it answers whether the SPI link to the controller is alive independent of any one pin. `q` soaks for ghost touches once it is. |
| A task looks starved or a stack looks tight | `k` | Reports per-task stack high-water marks, the task count, and input counters — the first read for "is something overflowing or not running". |
| Ethernet link behaves oddly | `e`, `a`, `n`, then `m` | `e`/`a`/`n` talk to the PHY over MDIO *below* lwIP, so they work whether or not a netif is up. `m` reads the MAC's own MMC counters over no bus at all — it answers even when the PHY is unresponsive. Suspect the wire before the stack. |
| A storage (`u`) gate fails | `y` | The raw flash-driver diagnostic, bypassing littlefs entirely — it isolates a hardware/driver fault from a filesystem one. |
| Display throughput seems off | `V` | Re-measures full-screen flush throughput under real scheduler + live-netif contention, so a regression shows up as a number. |

`Q` is worth calling out because it is *not* in the reference table above — it is
a board-only diagnostic (`#ifdef CADS_TARGET_ITSBOARD`) added mid-investigation
when a live report of "touch never registering" turned out to be `TP_IRQ`
(PE13) never toggling, found with a port-wide `w` watch. It is the shape a good
diagnostic takes: it removes one suspected pin from the question so the rest can
be answered.

For the full command set, arguments, and defaults, see
[the explorer console reference](../reference/explorer-console.md). To drive one
command non-interactively from a host shell rather than an open terminal, use
`scripts/board_cmd.py <letter> [arg] --timeout N`.

## Debugging the tooling itself

The host-side tools are dependency-free Python 3 on purpose — the hardware gate
has to run on a bare checkout with nothing but a system Python — which means the
tooling occasionally has to get a low-level detail right that a library would
have hidden. The canonical example is worth internalising before you lose an
hour to it.

If the console is silent and you are tempted to loop `stty` over baud rates:
**do not.** On macOS, `stty -f /dev/cu.usbmodemXXXX 115200 raw` opens the
device, applies the settings, and closes it again — and closing the last
descriptor resets the line discipline, so the next reader gets the 9600 default
no matter what was requested. Every baud rate produces identical garbage, and it
looks exactly like a hardware fault. The fix is to open the port once and set
`termios` on that same descriptor, which is what `scripts/cads_serial.py`'s
`open_console()` does and why the module exists at all — its header comment is
the primary account. The operational version of this, from the
gate's side, is in [Run the hardware gate](board-test.md#if-the-console-stays-silent);
reach for `cads_serial.py` rather than shelling out to `stty`.
