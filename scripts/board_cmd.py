#!/usr/bin/env python3
"""CaDS Zero - send one hardware-explorer command and capture its output.

board_test.py proves the boot-time self test; board_soak.py drives load over
hours. Neither can ask the explorer a one-off question - "run the touch soak",
"hold the app tree live for 30 seconds" - and print exactly what came back.
This is that: open the console, send one line, print everything that arrives
until `--timeout` runs out. Pick the timeout to comfortably cover whatever the
command itself is expected to take - there is no idle/quiet detection here,
because a command that produces a long silence in the middle of legitimate
work (the app demo just sitting there waiting for a keypress, say) would be
cut off by one.

Usage:
    scripts/board_cmd.py q 200            # touch soak, 200 samples
    scripts/board_cmd.py d 30 --timeout 40
    scripts/board_cmd.py k
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cads_serial import open_console, read_lines  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("letter", help="explorer command letter, e.g. q, d, k")
    parser.add_argument("argument", nargs="?", default="", help="optional numeric argument")
    parser.add_argument("--port", default=None,
                        help="serial device; default: $CADS_CONSOLE_PORT or the "
                             "first numeric /dev/cu.usbmodem* (the ST-Link VCP's "
                             "name shifts with the USB port, e.g. 11303 vs 1303)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=60.0, help="hard wall-clock deadline")
    args = parser.parse_args()

    port = args.port or os.environ.get("CADS_CONSOLE_PORT")
    if port is None:
        import glob
        # the ST-Link VCP enumerates with a purely numeric suffix; other CDC
        # devices (an LG monitor's control interface, say) carry letters
        candidates = [c for c in sorted(glob.glob("/dev/cu.usbmodem*"))
                      if c.rsplit("usbmodem", 1)[1].isdigit()]
        if not candidates:
            sys.exit("no ST-Link VCP found (no numeric /dev/cu.usbmodem*) - "
                     "pass --port or set CADS_CONSOLE_PORT")
        port = candidates[0]
    fd = open_console(port, args.baud)
    try:
        command = f"{args.letter} {args.argument}".strip() + "\r\n"
        os.write(fd, command.encode())
        for _line in read_lines(fd, timeout=args.timeout, echo=True):
            pass
        return 0
    finally:
        os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
