#!/usr/bin/env python3
"""CaDS Zero - simulate button presses on the running touchscreen UI.

There is no way to physically touch the panel from a Mac terminal, and the
app-tree's own interactive session (apps/bringup/explorer_app_demo.c, what
boot.autostart hands the panel to, and the console's own `d` command) never
reacts to a plain typed byte - so a normal typed command can't double as a
keypress, and can't accidentally end the session either.

The firmware reserves one byte per logical key, all >= 0x80 so they can
never collide with an ordinary typed ASCII command (0x20-0x7E) or CR/LF
(0x0D/0x0A) - see cads_explorer_app_demo_decode_key() in
apps/bringup/explorer_app_demo.c for the authoritative mapping and the full
reasoning. Sending one of these bytes calls cads_gui_input() directly (the
same function a real button or touch event reaches through
cads_gui_attach_input()'s trampoline) and the app-tree loop keeps running
instead of exiting - so this script can drive several keys in a row without
ever losing the interactive session, unlike board_cmd.py's own commands.

"quit" is the one byte in that range that is NOT a real button - it is the
only thing that ends the session (2026-08-29, replacing the old "any byte
exits" rule, which meant a stray diagnostic command could kill the session
by accident). Send it before running any plain board_cmd.py command while
the app tree is live, then `board_key.py`/`d` again afterward to resume.

Usage:
    scripts/board_key.py down down ok       # navigate into the 3rd item
    scripts/board_key.py back
    scripts/board_key.py --delay 0.3 up up ok
    scripts/board_key.py quit               # back to the console prompt
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cads_serial import open_console, resolve_console_port  # noqa: E402

# Must match cads_explorer_app_demo_decode_key() and CADS_APP_DEMO_EXIT_BYTE
# in apps/bringup/explorer_app_demo.c exactly.
KEY_BYTES = {
    "up": 0x80,
    "down": 0x81,
    "left": 0x82,
    "right": 0x83,
    "ok": 0x84,
    "back": 0x85,
    "f1": 0x86,
    "f2": 0x87,
    "quit": 0x88,  # not a real button - ends the session, see module docstring
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("keys", nargs="+", choices=sorted(KEY_BYTES),
                        help="one or more logical keys, sent in order")
    parser.add_argument("--port", default=None,
                        help="serial device; default: $CADS_CONSOLE_PORT, the "
                             "first numeric /dev/cu.usbmodem*, or (inside the "
                             "firmware-lab course container) the board-bridge's "
                             "console PTY")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--delay", type=float, default=0.15,
                        help="seconds between keys, so the GUI has a tick to "
                             "redraw before the next one lands (default 0.15)")
    args = parser.parse_args()

    port = resolve_console_port(args.port)

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
