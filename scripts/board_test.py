#!/usr/bin/env python3
"""CaDS Zero - on-target test gate.

Builds the firmware, flashes it to the real ITSboard, resets it, and reads the
TAP stream the bring-up self test emits over the ST-Link virtual COM port.
Exits non-zero if any assertion failed, if the announced number of assertions
did not arrive, or if the board went quiet.

This is the gate every milestone has to clear. A green CI build that never
touched hardware proves very little about firmware whose display bus is
write-only and whose SPI shares a pin with the Ethernet PHY.

Every external command runs under a timeout. Debug probes and serial ports hang;
a gate that can hang forever is worse than one that fails.

PER-MILESTONE SUITES
---------------------
The boot-time self test is the one thing every milestone's firmware always
emits, unconditionally, in real TAP - that half of "TAP over VCP, per-milestone
suites" already existed. The other half did not: nothing tied a later
milestone's own explorer commands (board_cmd.py's `f`/`d`/`l`/`k` and friends)
into a repeatable, scriptable pass/fail check the way the boot self test is one.

`--suite <name>` adds that without touching the firmware at all: after a clean
boot-gate pass, it sends each suite's explorer commands over the same open
console, and synthesises its own TAP stream from a small per-check validator
function - not from the firmware, which mostly prints ad hoc "# foo: done"
text, not `ok N`. See SUITES below for what each one checks and why each
check's PASS/FAIL condition was chosen the way it was (several of this
project's own explorer commands have an environment-dependent correct
answer - see docs/ROADMAP.md's own repeated notes on this bench having no
DHCP server and no way to attach a physical jumper wire - so a suite checks
that a command *completed and reported a well-formed result*, not that the
result was unconditionally "good").

Usage:
    scripts/board_test.py                    # build, flash, run
    scripts/board_test.py --no-build         # flash the existing image and run
    scripts/board_test.py --no-flash         # reset whatever is on the board and listen
    scripts/board_test.py --port /dev/cu.usbmodem11303 --timeout 30
    scripts/board_test.py --list-ports
    scripts/board_test.py --suite m6         # boot gate, then the M6 GPIO suite
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cads_serial import open_console, read_lines  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BAUD = 115200
DEFAULT_STLINK = "066FFF565282494867161033"

RESULT_RE = re.compile(r"^#\s*RESULT:\s*(PASS|FAIL)\s*$")
PLAN_RE = re.compile(r"^1\.\.(\d+)\s*$")
TAP_RE = re.compile(r"^(ok|not ok)\s+(\d+)\s*-?\s*(.*)$")

# A completion marker shared across every suite check below, so run_suite()
# can stop reading as soon as a command is actually done rather than sitting
# out its own worst-case timeout every time - the same reason the boot gate
# itself stops on RESULT_RE rather than reading until args.timeout expires.
SUITE_DONE_RE = re.compile(
    r"^#\s*(freq|pwm|logic):\s*(done|captured)\b|^#\s*continuity:\s*(PASS|FAIL)\b"
)


def _seen(pattern: str):
    """A validator: some line in the command's output matched `pattern`."""
    rx = re.compile(pattern)
    return lambda lines: any(rx.match(line) for line in lines)


def _seen_and_not(pattern: str, forbidden: str):
    """A validator: `pattern` matched, and `forbidden` never appeared -
    e.g. "the capture finished" AND "the redraw never timed out"."""
    rx, bad = re.compile(pattern), re.compile(forbidden)
    return lambda lines: any(rx.match(line) for line in lines) and not any(
        bad.search(line) for line in lines
    )


# Each check is (explorer letter, argument, description, validator). The
# argument picks a short duration deliberately - this is a repeatable gate
# meant to run every time, not a demo - and every validator asks "did this
# command complete and report a well-formed result", not "was the result
# unconditionally good": several of these commands have a correct answer
# that depends on this bench's own environment (no DHCP server, no way to
# attach a physical jumper wire - see docs/ROADMAP.md's own notes on both),
# so asserting a specific PASS would make the suite fail on a perfectly
# healthy board.
SUITES: dict[str, list[tuple[str, str, str, object]]] = {
    "m6": [
        (
            "F",
            "2",
            "frequency/duty-cycle counter (CN8 pin 5) completes cleanly",
            _seen(r"^#\s*freq:\s*done,"),
        ),
        (
            "D",
            "1000 50 1",
            "PWM generator (OUT13) completes cleanly",
            _seen(r"^#\s*pwm:\s*done"),
        ),
        (
            "L",
            "25 1",
            "logic analyzer captures and renders without a redraw timeout",
            _seen_and_not(r"^#\s*logic:\s*captured", r"redraw did not complete"),
        ),
        (
            "K",
            "",
            "continuity tester completes with a well-formed PASS/FAIL result",
            _seen(r"^#\s*continuity:\s*(PASS|FAIL)\b"),
        ),
    ],
}


def run_suite(fd: int, name: str, per_check_timeout: float) -> int:
    checks = SUITES[name]
    print(f"\n# suite: {name} ({len(checks)} check(s))", flush=True)
    print(f"1..{len(checks)}", flush=True)

    passed = 0
    for i, (letter, argument, description, validator) in enumerate(checks, start=1):
        command = f"{letter} {argument}".strip() + "\r\n"
        os.write(fd, command.encode())
        lines = list(read_lines(fd, timeout=per_check_timeout, stop_when=SUITE_DONE_RE.match))
        ok = validator(lines)
        print(f"{'ok' if ok else 'not ok'} {i} - {description}", flush=True)
        if ok:
            passed += 1

    result = "PASS" if passed == len(checks) else "FAIL"
    print(f"# RESULT: {result}", flush=True)
    return 0 if passed == len(checks) else 1


def candidate_ports() -> list[str]:
    """Serial devices that could plausibly be the ST-Link VCP.

    On macOS the ST-Link enumerates as /dev/cu.usbmodem<location-id>, where the
    suffix is purely numeric. Devices with letters in the suffix advertise their
    own USB serial string and are something else - excluding them avoids
    silently testing against an unrelated adapter, which cost real time once.
    """
    ports = []
    for path in sorted(glob.glob("/dev/cu.usbmodem*")):
        suffix = path.rsplit("usbmodem", 1)[-1]
        if suffix.isdigit():
            ports.append(path)
    ports += sorted(glob.glob("/dev/ttyACM*"))
    return ports


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    if env := os.environ.get("CADS_CONSOLE_PORT"):
        return env
    ports = candidate_ports()
    if not ports:
        sys.exit(
            "error: no ST-Link virtual COM port found.\n"
            "       Looked for /dev/cu.usbmodem<digits> and /dev/ttyACM*.\n"
            "       Pass --port explicitly or set CADS_CONSOLE_PORT."
        )
    if len(ports) > 1:
        print(f"note: several candidates {ports}, using {ports[0]}", file=sys.stderr)
    return ports[0]


def run(cmd: list[str], timeout: float, check: bool = True) -> int:
    print(f"$ {' '.join(cmd)}", flush=True)
    try:
        completed = subprocess.run(cmd, timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"error: `{cmd[0]}` did not finish within {timeout:.0f}s", file=sys.stderr)
        return 124
    if check and completed.returncode != 0:
        print(f"error: `{cmd[0]}` exited {completed.returncode}", file=sys.stderr)
    return completed.returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--port", help="serial device of the ST-Link VCP")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for RESULT")
    parser.add_argument("--build-timeout", type=float, default=300.0)
    parser.add_argument("--flash-timeout", type=float, default=60.0)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--no-flash", action="store_true")
    parser.add_argument("--build-type", default="Debug")
    parser.add_argument("--list-ports", action="store_true")
    parser.add_argument(
        "--suite",
        choices=sorted(SUITES),
        help="after a clean boot gate, also run this milestone's explorer-command suite",
    )
    parser.add_argument(
        "--suite-timeout", type=float, default=15.0, help="per-check deadline within a suite"
    )
    args = parser.parse_args()

    if args.list_ports:
        for path in candidate_ports():
            print(path)
        return 0

    if not args.no_build:
        if code := run(["bash", str(ROOT / "scripts" / "build.sh"), args.build_type],
                       timeout=args.build_timeout):
            return code

    port = find_port(args.port)
    print(f"# console: {port} @ {args.baud} 8N1", flush=True)

    # Open and configure BEFORE resetting, so the banner cannot be missed.
    # See cads_serial.py for why the configuration must happen on this very fd.
    fd = open_console(port, args.baud)

    try:
        if not args.no_flash:
            if code := run(["bash", str(ROOT / "scripts" / "flash.sh")], timeout=args.flash_timeout):
                return code
        else:
            serial = os.environ.get("CADS_STLINK_SERIAL", DEFAULT_STLINK)
            run(["st-flash", "--serial", serial, "reset"], timeout=args.flash_timeout, check=False)

        planned: int | None = None
        result: str | None = None
        failures: list[str] = []
        executed = 0

        for line in read_lines(fd, timeout=args.timeout, stop_when=RESULT_RE.match):
            if plan := PLAN_RE.match(line):
                planned = int(plan.group(1))
            if tap := TAP_RE.match(line):
                executed += 1
                if tap.group(1) == "not ok":
                    failures.append(line)
            if done := RESULT_RE.match(line):
                result = done.group(1)

        print(flush=True)

        if result is None:
            print(
                f"FAIL: no RESULT line within {args.timeout:.0f}s ({executed} assertions seen).\n"
                f"      Check that {port} really is the ST-Link VCP "
                f"(scripts/board_test.py --list-ports) and that the firmware got past init."
            )
            return 2

        if planned is not None and executed != planned:
            print(
                f"FAIL: plan announced {planned} assertions but {executed} arrived - "
                f"the firmware stopped part way through."
            )
            return 3

        if failures:
            print(f"FAIL: {len(failures)} assertion(s) failed:")
            for line in failures:
                print(f"  {line}")
            return 1

        if result != "PASS":
            print(f"FAIL: firmware reported {result}")
            return 1

        print(f"PASS: {executed}/{planned or executed} assertions on real hardware")

        # The suite runs over the same still-open console, on purpose: the
        # boot gate above is what proves the explorer REPL this needs is
        # actually alive to send commands to, not a separate concern.
        if args.suite:
            return run_suite(fd, args.suite, args.suite_timeout)
    finally:
        os.close(fd)

    return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
