#!/usr/bin/env python3
"""CaDS Zero - bake a TTF into packed 1 bpp glyph atlases.

WHY 1 BIT PER PIXEL
-------------------
The canvas is 4 bpp indexed (see docs/explanation/why-4bpp.md). Antialiased
glyphs would need intermediate shades, and in an indexed buffer those can only
come from palette slots - so antialiasing would either burn scarce slots on
grey ramps or force text to be drawn in one hard-coded colour. Crisp 1 bpp
glyphs stay drawable in any of the sixteen colours, which matters far more for
this UI than smooth edges do at 12-24 px.

The generated C is committed, so a build never needs the TTF or PIL.

Usage:
    scripts/gen_font.py --out gui/fonts/cads_fonts.c
    scripts/gen_font.py --font /path/to/Font.ttf --sizes 12 16 24
"""

from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("error: this generator needs Pillow (pip install Pillow). "
             "The generated C is committed, so only regeneration needs it.")

# Fonts whose licence permits redistributing derived bitmap data.
CANDIDATES = [
    ("JetBrains Mono", "SIL Open Font License 1.1",
     "/opt/homebrew/Caskroom/intellij-idea/*/IntelliJ IDEA.app/Contents/jbr/"
     "Contents/Home/lib/fonts/JetBrainsMono-Regular.ttf"),
    ("Droid Sans Mono", "Apache License 2.0",
     "/opt/homebrew/Caskroom/intellij-idea/*/IntelliJ IDEA.app/Contents/jbr/"
     "Contents/Home/lib/fonts/DroidSansMono.ttf"),
    ("DejaVu Sans Mono", "Bitstream Vera / Public Domain",
     "/opt/homebrew/**/DejaVuSansMono.ttf"),
]

FIRST, LAST = 0x20, 0x7E   # printable ASCII


def find_font(explicit: str | None) -> tuple[Path, str, str]:
    if explicit:
        return Path(explicit), Path(explicit).stem, "supplied on the command line"
    for name, licence, pattern in CANDIDATES:
        for hit in sorted(glob.glob(pattern, recursive=True)):
            return Path(hit), name, licence
    sys.exit("error: no suitable font found; pass --font explicitly")


def bake(font_path: Path, size: int) -> tuple[list[dict], bytearray, int, int]:
    font = ImageFont.truetype(str(font_path), size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyphs: list[dict] = []
    blob = bytearray()

    for code in range(FIRST, LAST + 1):
        char = chr(code)
        box = font.getbbox(char)
        advance = int(round(font.getlength(char)))

        if box is None or box[2] <= box[0] or box[3] <= box[1]:
            glyphs.append({"w": 0, "h": 0, "left": 0, "top": 0,
                           "advance": advance, "offset": len(blob)})
            continue

        left, top, right, bottom = box
        width, height = right - left, bottom - top

        image = Image.new("L", (width, height), 0)
        draw = ImageDraw.Draw(image)
        draw.text((-left, -top), char, font=font, fill=255)

        # Threshold at the midpoint. Higher keeps stems thin and loses hairlines
        # at 12 px; lower fattens everything into mush.
        row_bytes = (width + 7) // 8
        offset = len(blob)
        for y in range(height):
            row = bytearray(row_bytes)
            for x in range(width):
                if image.getpixel((x, y)) >= 128:
                    row[x >> 3] |= 0x80 >> (x & 7)
            blob += row

        glyphs.append({"w": width, "h": height, "left": left,
                       "top": ascent - top, "advance": advance, "offset": offset})

    return glyphs, blob, line_height, ascent


def emit(out: Path, font_path: Path, font_name: str, licence: str, sizes: list[int]) -> None:
    lines = [
        "/* GENERATED FILE - do not edit.\n",
        f" * Regenerate with: scripts/gen_font.py --font <ttf> --sizes {' '.join(map(str, sizes))}\n",
        " *\n",
        f" * Source font: {font_name}\n",
        f" * Licence:     {licence}\n",
        " *\n",
        " * Glyphs are 1 bpp, MSB first, rows padded to whole bytes. See\n",
        " * scripts/gen_font.py for why antialiasing is deliberately absent.\n",
        " */\n\n",
        '#include "font.h"\n\n',
    ]

    for size in sizes:
        glyphs, blob, line_height, ascent = bake(font_path, size)
        tag = f"cads_font{size}"

        lines.append(f"static const uint8_t {tag}_bitmap[] = {{\n")
        for i in range(0, len(blob), 16):
            chunk = ", ".join(f"0x{b:02X}" for b in blob[i:i + 16])
            lines.append(f"    {chunk},\n")
        lines.append("};\n\n")

        lines.append(f"static const cads_glyph_t {tag}_glyphs[] = {{\n")
        for code, g in zip(range(FIRST, LAST + 1), glyphs):
            printable = chr(code) if code != 0x27 and code != 0x5C else "?"
            lines.append(
                f"    {{{g['w']:3d}, {g['h']:3d}, {g['left']:4d}, {g['top']:4d},"
                f" {g['advance']:3d}, {g['offset']:5d}}},  /* '{printable}' */\n")
        lines.append("};\n\n")

        lines.append(
            f"const cads_font_t {tag} = {{\n"
            f'    .name = "{font_name} {size}",\n'
            f"    .line_height = {line_height},\n"
            f"    .ascent = {ascent},\n"
            f"    .first = 0x{FIRST:02X},\n"
            f"    .last = 0x{LAST:02X},\n"
            f"    .glyphs = {tag}_glyphs,\n"
            f"    .bitmap = {tag}_bitmap,\n"
            f"}};\n\n")

        print(f"  {tag}: {len(blob)} bytes of bitmap, line height {line_height}")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font")
    parser.add_argument("--sizes", type=int, nargs="+", default=[12, 16, 24])
    parser.add_argument("--out", default="gui/fonts/cads_fonts.c")
    args = parser.parse_args()

    path, name, licence = find_font(args.font)
    print(f"font: {name} ({licence})\n      {path}")
    emit(Path(args.out), path, name, licence, args.sizes)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
