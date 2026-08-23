#!/usr/bin/env python3
"""CaDS Zero - CI size regression budget for flash.

The linker (targets/itsboard/linker/cads_itsboard.ld) gives the firmware
image its own 1024 KB region, FLASH_APP, and refuses to link into
FLASH_FS (the littlefs volume at 0x08120000) at all - that is a hard
boundary, already checked separately by scripts/check_fs_window.py. It is
not, on its own, a budget: nothing before this script failed a build for
using more of FLASH_APP than intended, so a single careless change (an
embedded asset checked in by accident, a debug macro that expands per
call site, linking a library nothing actually calls) could grow the image
for a long time before a human happened to notice the number
`arm-none-eabi-size` already prints to every CI job's summary.

This reads the built ELF's own text+data size (the same two sections the
linker actually places `> FLASH_APP`, not re-derived from source) and
fails if it exceeds `--max-bytes`. The default, 512 KB, is half of
FLASH_APP: current usage (~222 KB, the `default` matrix leg, 2026-08-23)
has more than 2x headroom under it, so this catches an actual runaway,
not ordinary feature growth - and it leaves the other half of the region
alone, the same margin-not-maximum spirit as check_ram_budget.py's own
256 B floor.

Usage:
    scripts/check_flash_budget.py build/cads-zero.elf
    scripts/check_flash_budget.py build/cads-zero.elf --max-bytes 600000
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

FLASH_APP_BYTES = 1024 * 1024
DEFAULT_MAX_BYTES = 512 * 1024

# `size --format=berkeley` (the GNU default): one header line, one data line,
#    text    data     bss     dec     hex filename
#  227284     176  151472  378932   5c834 build/cads-zero.elf
SIZE_LINE_RE = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([0-9a-fA-F]+)\s+\S+\s*$")


def find_size_tool() -> str:
    for candidate in ("arm-none-eabi-size", "size"):
        if subprocess.run(["which", candidate], capture_output=True).returncode == 0:
            return candidate
    sys.exit("error: no arm-none-eabi-size (or size) on PATH")


def read_flash_bytes(elf_path: str, size_tool: str) -> int:
    result = subprocess.run([size_tool, elf_path], capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        sys.exit(f"error: `{size_tool} {elf_path}` failed:\n{result.stderr}")

    for line in result.stdout.splitlines():
        if match := SIZE_LINE_RE.match(line):
            text_bytes, data_bytes = int(match.group(1)), int(match.group(2))
            return text_bytes + data_bytes

    sys.exit(
        f"error: could not parse `{size_tool} {elf_path}` output as berkeley-format size:\n{result.stdout}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf", help="path to the built cads-zero.elf")
    parser.add_argument(
        "--max-bytes",
        type=int,
        default=DEFAULT_MAX_BYTES,
        help="fail if text+data (the flash image, .text/.rodata/.data's load size) exceeds this many bytes",
    )
    parser.add_argument("--size-tool", help="override the size binary (auto-detected otherwise)")
    args = parser.parse_args()

    size_tool = args.size_tool or find_size_tool()
    flash_bytes = read_flash_bytes(args.elf, size_tool)
    headroom = args.max_bytes - flash_bytes

    print(f"flash image (text+data) = {flash_bytes} B ({flash_bytes / 1024:.2f} K)")
    print(f"FLASH_APP region        = {FLASH_APP_BYTES} B (1024 K, the linker's own region)")
    print(f"budget                  = {args.max_bytes} B ({args.max_bytes / 1024:.2f} K)")

    if headroom < 0:
        print(
            f"FAIL: {flash_bytes} B exceeds the {args.max_bytes} B budget by {-headroom} B.\n"
            f"      This still links today (FLASH_APP has {FLASH_APP_BYTES - flash_bytes} B left), "
            f"but has grown past what this budget treats as ordinary feature growth - "
            f"check what just got bigger (arm-none-eabi-size -A, or `nm --size-sort`) before raising the budget."
        )
        return 1

    print(f"PASS: {headroom} B of headroom under the {args.max_bytes} B budget")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
