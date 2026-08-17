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

Usage:
    scripts/board_test.py                    # build, flash, run
    scripts/board_test.py --no-build         # flash the existing image and run
    scripts/board_test.py --no-flash         # reset whatever is on the board and listen
    scripts/board_test.py --port /dev/cu.usbmodem11303 --timeout 30
    scripts/board_test.py --list-ports
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
    finally:
        os.close(fd)

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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
