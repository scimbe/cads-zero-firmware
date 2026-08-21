#!/usr/bin/env python3
"""CaDS Zero - CI size regression budget for RAM.

targets/itsboard/linker/cads_itsboard.ld already refuses to link at all
once the SRAM heap drops below 48K (`ASSERT(__cads_heap_size >= 48K, ...)`
- lwIP and the GUI will not fit). That guard is a floor, not a budget: this
milestone's own history (docs/ROADMAP.md's own logged M5/M6 tasks) hit it
exactly - `__cads_heap_size` at *precisely* 48K, zero bytes of slack -
more than once, and every one of those was only caught by a human counting
bytes by hand after the fact. A build that merely links is not the same as
a build with any room left for the next feature.

This reads `__cads_heap_size` back out of the built ELF (the same symbol
the linker's own ASSERT already computes - not re-derived from section
sizes, which would risk drifting out of sync with what the linker script
actually does) and fails if the margin above that 48K floor is thinner
than `--min-margin-bytes`. The default, 256 B, is deliberately the
smallest margin this session ever accepted as "real, not razor-thin" for
a shipped task, not an arbitrary round number.

Usage:
    scripts/check_ram_budget.py build/cads-zero.elf
    scripts/check_ram_budget.py build/cads-zero.elf --min-margin-bytes 512
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

HEAP_FLOOR_BYTES = 48 * 1024

NM_LINE_RE = re.compile(r"^([0-9a-fA-F]+)\s+\S+\s+__cads_heap_size\s*$")


def find_nm() -> str:
    for candidate in ("arm-none-eabi-nm", "nm"):
        if subprocess.run(["which", candidate], capture_output=True).returncode == 0:
            return candidate
    sys.exit("error: no arm-none-eabi-nm (or nm) on PATH")


def read_heap_size(elf_path: str, nm: str) -> int:
    result = subprocess.run([nm, elf_path], capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        sys.exit(f"error: `{nm} {elf_path}` failed:\n{result.stderr}")

    for line in result.stdout.splitlines():
        if match := NM_LINE_RE.match(line):
            return int(match.group(1), 16)

    sys.exit(
        "error: __cads_heap_size not found in the symbol table - "
        "is this build/cads-zero.elf, and does the linker script still define it?"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf", help="path to the built cads-zero.elf")
    parser.add_argument(
        "--min-margin-bytes",
        type=int,
        default=256,
        help="fail if __cads_heap_size is within this many bytes of the linker's own 48K floor",
    )
    parser.add_argument("--nm", help="override the nm binary (auto-detected otherwise)")
    args = parser.parse_args()

    nm = args.nm or find_nm()
    heap_size = read_heap_size(args.elf, nm)
    margin = heap_size - HEAP_FLOOR_BYTES

    print(f"__cads_heap_size = {heap_size} B ({heap_size / 1024:.2f} K)")
    print(f"floor            = {HEAP_FLOOR_BYTES} B (48 K, the linker's own ASSERT)")
    print(f"margin           = {margin} B")

    if margin < 0:
        # Unreachable in practice - the linker's own ASSERT would already
        # have failed the build before this script ever ran - but checked
        # explicitly rather than trusted, in case that guard is ever loosened.
        print(f"FAIL: heap size is already below the linker's 48K floor by {-margin} B")
        return 1

    if margin < args.min_margin_bytes:
        print(
            f"FAIL: only {margin} B of margin over the 48K floor, "
            f"budget requires at least {args.min_margin_bytes} B.\n"
            f"      This links today, but the next RAM-costing feature might not - "
            f"trim something (see docs/ROADMAP.md's own M5/M6 tasks for the established "
            f"levers: modules/net/include/lwipopts.h's pool sizes, or the feature's own "
            f"static buffer)."
        )
        return 1

    print(f"PASS: {margin} B of margin, budget is {args.min_margin_bytes} B")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
