# Run the hardware gate

```bash
scripts/board_test.py                 # build, flash, run
scripts/board_test.py --no-build      # flash the existing image and run
scripts/board_test.py --no-flash      # reset whatever is on the board and listen
scripts/board_test.py --list-ports
```

It builds, flashes, resets the board, and reads a TAP stream over the ST-Link's
virtual COM port. Exit code 0 only if every assertion passed and the announced
count arrived.

## Why this exists

The display bus is write-only, so software cannot ask the panel whether a write
landed. A green CI build proves the code compiles, not that it runs on silicon.
No milestone is done until this passes.

## Reading a failure

| Exit | Meaning |
|---|---|
| 1 | An assertion reported `not ok`, or the firmware reported FAIL |
| 2 | No `# RESULT:` line arrived — the board faulted, or the port is wrong |
| 3 | The plan announced N assertions and fewer arrived — the firmware died part way |

Code 3 is the useful one: it catches a firmware that stops half way through and
would otherwise look like it simply printed less.

## Picking the port

The ST-Link's VCP enumerates as `/dev/cu.usbmodem<digits>` on macOS. Devices with
letters in the suffix carry their own USB serial string and are something else,
so they are filtered out — testing against an unrelated adapter cost real time
once.

```bash
scripts/board_test.py --port /dev/cu.usbmodem11303
CADS_CONSOLE_PORT=/dev/cu.usbmodem11303 scripts/board_test.py
```

## If the console stays silent

Do not reach for `stty` and a loop over baud rates. `stty -f /dev/cu.X` opens
the device, applies the settings, and closes it again — and closing the last
descriptor resets the line discipline, so the next reader gets the 9600 default
no matter what was requested. Every baud rate you try produces the same garbage
and it looks exactly like a hardware fault.

The baud rate must be set with `termios` on the same descriptor that is read
from. `scripts/cads_serial.py` does that; use it rather than shelling out.

## Everything runs under a timeout

Probes and serial ports hang. Every subprocess call and every read has a
deadline, because a gate that can hang forever is worse than one that fails.
