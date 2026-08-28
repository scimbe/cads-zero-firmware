#!/usr/bin/env python3
"""CaDS Zero - simulate button presses on the running touchscreen UI.

There is no way to physically touch the panel from a Mac terminal, and the
app-tree's own interactive session (apps/bringup/explorer_app_demo.c, what
boot.autostart hands the panel to) drops back to the plain console prompt on
ANY console byte - so a normal typed command can't double as a keypress
without ending the very session it would be driving.

The firmware reserves one byte per logical key, all >= 0x80 so they can
never collide with an ordinary typed ASCII command (0x20-0x7E) or CR/LF
(0x0D/0x0A) - see cads_explorer_app_demo_decode_key() in
apps/bringup/explorer_app_demo.c for the authoritative mapping and the full
reasoning. Sending one of these bytes calls cads_gui_input() directly (the
same function a real button or touch event reaches through
cads_gui_attach_input()'s trampoline) and the app-tree loop keeps running
instead of exiting - so this script can drive several keys in a row without
ever losing the interactive session, unlike board_cmd.py's own commands.

Usage:
    scripts/board_key.py down down ok       # navigate into the 3rd item
    scripts/board_key.py back
    scripts/board_key.py --delay 0.3 up up ok
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cads_serial import open_console  # noqa: E402

# Must match cads_explorer_app_demo_decode_key() in
# apps/bringup/explorer_app_demo.c exactly.
KEY_BYTES = {
    "up": 0x80,
    "down": 0x81,
    "left": 0x82,
    "right": 0x83,
    "ok": 0x84,
    "back": 0x85,
    "f1": 0x86,
    "f2": 0x87,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("keys", nargs="+", choices=sorted(KEY_BYTES),
                        help="one or more logical keys, sent in order")
    parser.add_argument("--port", default=None,
                        help="serial device; default: $CADS_CONSOLE_PORT or the "
                             "first numeric /dev/cu.usbmodem*")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--delay", type=float, default=0.15,
                        help="seconds between keys, so the GUI has a tick to "
                             "redraw before the next one lands (default 0.15)")
    args = parser.parse_args()

    port = args.port or os.environ.get("CADS_CONSOLE_PORT")
    if port is None:
        import glob
        candidates = [c for c in sorted(glob.glob("/dev/cu.usbmodem*"))
                      if c.rsplit("usbmodem", 1)[1].isdigit()]
        if not candidates:
            sys.exit("no ST-Link VCP found (no numeric /dev/cu.usbmodem*) - "
                     "pass --port or set CADS_CONSOLE_PORT")
        port = candidates[0]

    fd = open_console(port, args.baud)
    try:
        for i, key in enumerate(args.keys):
            if i > 0:
                time.sleep(args.delay)
            os.write(fd, bytes([KEY_BYTES[key]]))
            print(f"  | sent: {key}", flush=True)
        return 0
    finally:
        os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
