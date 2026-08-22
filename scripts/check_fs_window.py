#!/usr/bin/env python3
"""CaDS Zero - CI check that no section actually occupies the littlefs
flash window (0x08120000, flash bank 2 - see docs/SAFETY.md).

Replaces a grep-based regex check in .github/workflows/ci.yml
(`grep -qE '0810[2-9a-f]|081[1-9a-f]'` against `objdump -h`'s raw text)
that was never anchored to the address column at all - a substring
search across a whole section line, which also includes its SIZE, file
offset and alignment. Caught live, not by inspection: the M5 ssdpwatch
task's "minimal apps" CI build shifted `.dmaram`'s VMA to 0x20008144,
and "0814" - a substring of THAT address, not a flash address at all -
matched the regex's own `081[1-9a-f]` alternative. No section anywhere
in that build was within a hundred megabytes of the real littlefs
window; the CI job still failed.

This reads each section's own LMA (load address - where the linker
actually placed it in flash) via `arm-none-eabi-objdump -h`, parsed by
column position rather than a substring search, and only flags a
section that both has the LOAD flag (something genuinely occupies flash
there - `.bss`/`.dmaram`/`.heap` are ALLOC-only, no real bytes stored,
and routinely carry a leftover/bookkeeping LMA field from the linker
script that means nothing about real flash occupancy) and whose LMA
range actually overlaps the littlefs window.

Usage:
    scripts/check_fs_window.py build/cads-zero.elf
"""

from __future__ import annotations

import re
import subprocess
import sys

FS_WINDOW_START = 0x08120000
FS_WINDOW_SIZE = 896 * 1024  # littlefs volume, flash bank 2 - see docs/SAFETY.md
FS_WINDOW_END = FS_WINDOW_START + FS_WINDOW_SIZE

# objdump -h prints one two-line record per section:
#   Idx Name          Size      VMA       LMA       File off  Algn
#     N .name         hhhhhhhh  hhhhhhhh  hhhhhhhh  hhhhhhhh  2**n
#                      CONTENTS, ALLOC, LOAD, ...
SECTION_LINE_RE = re.compile(
    r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+2\*\*\d+\s*$"
)


def find_objdump() -> str:
    for candidate in ("arm-none-eabi-objdump", "objdump"):
        if subprocess.run(["which", candidate], capture_output=True).returncode == 0:
            return candidate
    sys.exit("error: no arm-none-eabi-objdump (or objdump) on PATH")


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <elf>")
    elf = sys.argv[1]
    objdump = find_objdump()

    result = subprocess.run([objdump, "-h", elf], capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        sys.exit(f"error: `{objdump} -h {elf}` failed:\n{result.stderr}")

    lines = result.stdout.splitlines()
    violations: list[tuple[str, int, int]] = []

    for i, line in enumerate(lines):
        m = SECTION_LINE_RE.match(line)
        if not m:
            continue
        name, size_hex, _vma_hex, lma_hex = m.groups()
        size = int(size_hex, 16)
        lma = int(lma_hex, 16)
        if size == 0:
            continue

        flags_line = lines[i + 1] if i + 1 < len(lines) else ""
        if "LOAD" not in flags_line:
            continue  # ALLOC-only (.bss/.dmaram/.heap) - no real flash bytes land here

        if lma < FS_WINDOW_END and (lma + size) > FS_WINDOW_START:
            violations.append((name, lma, size))

    if violations:
        print("FAIL: section(s) occupy the littlefs flash window "
              f"(0x{FS_WINDOW_START:08x}-0x{FS_WINDOW_END:08x}):")
        for name, lma, size in violations:
            print(f"  {name}: LMA=0x{lma:08x} size=0x{size:x}")
        return 1

    print(f"PASS: no LOAD section occupies the littlefs window "
          f"(0x{FS_WINDOW_START:08x}-0x{FS_WINDOW_END:08x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
