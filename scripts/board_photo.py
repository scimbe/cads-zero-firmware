#!/usr/bin/env python3
"""CaDS Zero - photograph the panel through the bench camera.

WHY THIS EXISTS
---------------
The display bus is write-only: there is no readback path through the shield's
shift register chain, so no software test can confirm what is actually on the
screen. A camera closes that loop, and it earns its keep immediately - the very
first text rendered on the board came out mirrored, a fault that every
colour-bar test pattern had passed because such patterns are symmetric in X.

Pairs with the hardware explorer: drive a pattern over the console, photograph
it, look. Deliberately not automated into an image comparison yet; the lighting
at the bench is not controlled enough for that to mean anything.

THE CAMERA IS SELECTED BY NAME, NEVER BY INDEX.
avfoundation numbers devices in enumeration order, and that order changes the
moment any other camera appears or disappears - a virtual camera starting, a
capture device being plugged in. Addressing the bench camera as "0" produced a
photograph of the room, which read exactly like the board having been moved and
cost a round of confused debugging. Naming it cannot drift.

Usage:
    scripts/board_photo.py                       # full frame
    scripts/board_photo.py --panel               # cropped to the panel
    scripts/board_photo.py --pattern 6 --panel   # draw, then photograph
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

# The board sits rotated 180 degrees relative to the camera, so every capture
# is rotated back. transpose=2 twice is a true 180 degree rotation - it
# preserves handedness, which matters when the thing being checked IS a mirror.
ROTATE = "transpose=2,transpose=2"

# Fraction of the rotated frame occupied by the panel. Re-measure if the camera
# or the board moves.
PANEL_CROP = "crop=iw*0.52:ih*0.60:iw*0.44:ih*0.10,scale=1400:-1"


DEFAULT_CAMERA = "HD Pro Webcam C920"


def list_devices(timeout: float = 30.0) -> list[tuple[int, str]]:
    """Enumerate avfoundation video devices as (index, name)."""
    result = subprocess.run(
        ["ffmpeg", "-f", "avfoundation", "-list_devices", "true", "-i", ""],
        capture_output=True, text=True, timeout=timeout)
    devices = []
    in_video = False
    for line in result.stderr.splitlines():
        if "AVFoundation video devices" in line:
            in_video = True
            continue
        if "AVFoundation audio devices" in line:
            break
        if not in_video:
            continue
        match = re.search(r"\[(\d+)\]\s+(.*)$", line)
        if match:
            devices.append((int(match.group(1)), match.group(2).strip()))
    return devices


def resolve_camera(wanted: str) -> str:
    """Map a camera name to its current index, failing loudly if absent.

    Returning the wrong camera silently is worse than not taking a photograph
    at all: the picture looks plausible and the conclusion drawn from it is
    wrong.
    """
    if wanted.isdigit():
        return wanted  # explicit index, caller's problem

    devices = list_devices()
    for index, name in devices:
        if wanted.lower() in name.lower():
            return str(index)

    available = ", ".join(f"[{i}] {n}" for i, n in devices) or "none"
    sys.exit(f"error: no video device matching {wanted!r}. Available: {available}")


def capture(out: Path, crop: bool, device: str, timeout: float) -> None:
    filters = ROTATE + ("," + PANEL_CROP if crop else "")
    cmd = [
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-f", "avfoundation", "-framerate", "30", "-video_size", "1920x1080",
        "-i", device,
        # Skip frames so the webcam's auto-exposure has settled; the first few
        # are always too dark to read a panel by.
        "-frames:v", "25", "-update", "1", "-vf", filters, str(out),
    ]
    subprocess.run(cmd, check=True, timeout=timeout,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def draw_pattern(pattern: int, port: str, settle: float) -> None:
    from cads_serial import open_console, read_lines
    import os

    fd = open_console(port, 115200)
    try:
        os.write(fd, b"b 90\r\n")
        time.sleep(0.4)
        for _ in read_lines(fd, timeout=1.5, echo=False):
            pass
        os.write(fd, f"p {pattern}\r\n".encode())
        for line in read_lines(fd, timeout=15.0, stop_when=lambda l: "drawn" in l, echo=True):
            pass
    finally:
        os.close(fd)
    time.sleep(settle)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default="board.jpg")
    parser.add_argument("--panel", action="store_true", help="crop to the display")
    parser.add_argument("--pattern", type=int, help="draw this pattern first")
    parser.add_argument("--port", default="/dev/cu.usbmodem11303")
    parser.add_argument("--camera", default=DEFAULT_CAMERA,
                        help="camera name (substring) or explicit index")
    parser.add_argument("--list-cameras", action="store_true")
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()

    if args.list_cameras:
        for index, name in list_devices():
            print(f"[{index}] {name}")
        return 0

    device = resolve_camera(args.camera)
    print(f"# camera: {args.camera!r} -> index {device}", flush=True)

    if args.pattern is not None:
        draw_pattern(args.pattern, args.port, args.settle)

    out = Path(args.out)
    capture(out, args.panel, device, args.timeout)
    print(f"wrote {out} ({out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
