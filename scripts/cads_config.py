#!/usr/bin/env python3
"""CaDS Zero - edit /config.txt on the board from the Mac (or any host).

The board's filesystem is not USB-mounted - there is no mass-storage
interface, only SWD (st-flash). This script is the practical stand-in for
"mount the filesystem": it dumps the littlefs volume (flash bank 2,
0x08120000, 896 KB) over SWD, edits one file inside it using `cads_fs` (built
from build/host, the exact same littlefs code the firmware runs - see
tools/cads_fs.c), and writes the volume back. The firmware itself never needs
to be reflashed for this; only the filesystem region is touched.

    scripts/cads_config.py pull [-o FILE]      # save the board's config.txt locally
    scripts/cads_config.py push FILE           # write FILE onto the board as config.txt
    scripts/cads_config.py edit [--editor CMD] # pull, open $EDITOR, push if you saved

After a push, use the firmware's own Settings -> Reload config to apply it
without a reboot (or just power-cycle - it is read at boot too).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CADS_ROOT = Path(__file__).resolve().parent.parent
FS_BASE = "0x08120000"
FS_SIZE = 0xE0000  # 896 KiB - see targets/itsboard/board.h CADS_FS_BLOCK_SIZE * CADS_FS_SECTOR_COUNT
FS_PATH = "/config.txt"


def stlink_serial():
    return os.environ.get("CADS_STLINK_SERIAL", "066FFF565282494867161033")


def cads_fs_bin():
    """Build (if needed) and return the path to the host cads_fs tool."""
    build_dir = CADS_ROOT / "build" / "host"
    exe = build_dir / ("cads_fs.exe" if os.name == "nt" else "cads_fs")
    if not exe.exists():
        print("building the host cads_fs tool (first run only)...", file=sys.stderr)
        subprocess.run(["cmake", "-S", str(CADS_ROOT), "-B", str(build_dir)], check=True)
        subprocess.run(["cmake", "--build", str(build_dir), "--target", "cads_fs"], check=True)
    if not exe.exists():
        sys.exit(f"cads_fs did not build at {exe}")
    return exe


def dump_fs(image_path):
    subprocess.run(
        ["st-flash", "--serial", stlink_serial(), "read", str(image_path), FS_BASE, hex(FS_SIZE)],
        check=True,
    )


def write_fs(image_path):
    # A push is a whole-volume read-modify-write: the image was dumped a moment
    # ago, edited, and is now written back in full. Any change the board itself
    # made to its filesystem in between (a calibration save, a config write from
    # the panel) is in the FRESH board volume but NOT in this stale image, and
    # writing the image back would overwrite it. The board should be idle - not
    # mid-write - during an edit; the safest moment is right after a reset,
    # before touching the panel.
    sys.stderr.write(
        "note: writing the whole filesystem back - make sure the board has not "
        "written its own storage since the pull (see docs).\n")
    subprocess.run(
        ["st-flash", "--serial", stlink_serial(), "write", str(image_path), FS_BASE],
        check=True,
    )


def cmd_pull(args):
    fs_tool = cads_fs_bin()
    with tempfile.TemporaryDirectory() as tmp:
        image = Path(tmp) / "fs.img"
        print("reading the filesystem region off the board...", file=sys.stderr)
        dump_fs(image)
        out = args.out or Path("config.txt")
        rc = subprocess.run([str(fs_tool), str(image), "get", FS_PATH, str(out)]).returncode
        if rc != 0:
            sys.exit(rc)
        print(f"saved {out}")


def cmd_push(args):
    if not args.file.exists():
        sys.exit(f"no such file: {args.file}")
    fs_tool = cads_fs_bin()
    with tempfile.TemporaryDirectory() as tmp:
        image = Path(tmp) / "fs.img"
        print("reading the filesystem region off the board...", file=sys.stderr)
        dump_fs(image)
        rc = subprocess.run([str(fs_tool), str(image), "put", FS_PATH, str(args.file)]).returncode
        if rc != 0:
            sys.exit(rc)
        print("writing the filesystem region back to the board...", file=sys.stderr)
        write_fs(image)
        print(f"pushed {args.file} as {FS_PATH}. "
              "Reload it from the panel (Settings -> Reload config) or power-cycle.")


def cmd_edit(args):
    fs_tool = cads_fs_bin()
    editor = args.editor or os.environ.get("EDITOR") or (
        "notepad" if os.name == "nt" else "nano")
    with tempfile.TemporaryDirectory() as tmp:
        image = Path(tmp) / "fs.img"
        local = Path(tmp) / "config.txt"
        print("reading the filesystem region off the board...", file=sys.stderr)
        dump_fs(image)
        rc = subprocess.run([str(fs_tool), str(image), "get", FS_PATH, str(local)]).returncode
        if rc != 0:
            sys.exit(rc)
        before = local.read_bytes()

        subprocess.run([editor, str(local)], check=True)

        after = local.read_bytes()
        if after == before:
            print("no changes - nothing pushed")
            return

        rc = subprocess.run([str(fs_tool), str(image), "put", FS_PATH, str(local)]).returncode
        if rc != 0:
            sys.exit(rc)
        print("writing the filesystem region back to the board...", file=sys.stderr)
        write_fs(image)
        print("pushed. Reload it from the panel (Settings -> Reload config) or power-cycle.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_pull = sub.add_parser("pull", help="save the board's config.txt locally")
    p_pull.add_argument("-o", "--out", type=Path, help="output path (default: ./config.txt)")
    p_pull.set_defaults(func=cmd_pull)

    p_push = sub.add_parser("push", help="write a local file onto the board as config.txt")
    p_push.add_argument("file", type=Path)
    p_push.set_defaults(func=cmd_push)

    p_edit = sub.add_parser("edit", help="pull, open $EDITOR, push if changed")
    p_edit.add_argument("--editor", help="override $EDITOR")
    p_edit.set_defaults(func=cmd_edit)

    args = ap.parse_args()
    if shutil.which("st-flash") is None:
        sys.exit("st-flash not found on PATH - install stlink-tools (brew install stlink / "
                 "apt install stlink-tools) or add it to PATH")
    args.func(args)


if __name__ == "__main__":
    main()
