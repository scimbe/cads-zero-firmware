#!/usr/bin/env python3
"""CaDS Zero - convert PNG artwork into 4 bpp indexed C arrays.

The canvas is 4 bpp indexed against a fixed sixteen-colour palette
(gui/canvas.h), so artwork has to be quantised to those exact colours rather
than dithered to something approximate. For the CaDS mark that is not a
compromise: the logo is drawn in three flat brand colours that are already
palette slots 2, 3 and 4, so the quantisation is lossless for the parts that
matter and only the antialiased edges get snapped.

Alpha becomes the transparent index, so a logo composites over any background.

Usage:
    scripts/gen_asset.py assets/logo/cads_logo_source.png --name cads_logo \
        --width 400 --out gui/assets/cads_logo.c
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("error: needs Pillow. The generated C is committed, so only "
             "regeneration needs it.")

# Must match cads_color_t in gui/canvas.h, in slot order.
PALETTE = [
    (0x00, 0x00, 0x00),  # 0  black
    (0xFF, 0xFF, 0xFF),  # 1  white
    (0x20, 0x4C, 0x86),  # 2  brand blue
    (0xB5, 0xC4, 0xD8),  # 3  lion blue
    (0x9C, 0xB3, 0x3B),  # 4  accent green
    (0x30, 0x35, 0x40),  # 5  gray dark
    (0x6B, 0x74, 0x80),  # 6  gray
    (0xC8, 0xCE, 0xD6),  # 7  gray light
    (0xC0, 0x39, 0x2B),  # 8  red
    (0xE0, 0xA0, 0x00),  # 9  amber
    (0x1F, 0x8A, 0x80),  # 10 teal
    (0x12, 0x30, 0x5A),  # 11 brand dark
    (0xF2, 0xF5, 0xF9),  # 12 surface
    (0xA0, 0x30, 0x70),  # 13 magenta
    (0x8F, 0xA6, 0xC4),  # 14 lion mid
    (0x10, 0x14, 0x18),  # 15 background
]

TRANSPARENT = 15  # background slot doubles as the transparent key


def nearest(rgb: tuple[int, int, int], exclude: set[int]) -> int:
    best, best_distance = 0, None
    for index, entry in enumerate(PALETTE):
        if index in exclude:
            continue
        # Weighted to match perceived brightness rather than raw euclidean
        # distance; without the weights the pale lion blue snaps to white.
        dr = (rgb[0] - entry[0]) * 0.30
        dg = (rgb[1] - entry[1]) * 0.59
        db = (rgb[2] - entry[2]) * 0.11
        distance = dr * dr + dg * dg + db * db
        if best_distance is None or distance < best_distance:
            best, best_distance = index, distance
    return best


def convert(path: Path, width: int | None, crop: tuple[int, int, int, int] | None):
    image = Image.open(path).convert("RGBA")
    if crop:
        image = image.crop(crop)
    if width and width != image.width:
        height = round(image.height * width / image.width)
        image = image.resize((width, height), Image.LANCZOS)

    pixels = image.load()
    rows = []
    used = set()
    for y in range(image.height):
        row = bytearray((image.width + 1) // 2)
        for x in range(image.width):
            r, g, b, a = pixels[x, y]
            index = TRANSPARENT if a < 128 else nearest((r, g, b), {TRANSPARENT})
            used.add(index)
            if x & 1:
                row[x >> 1] |= index
            else:
                row[x >> 1] |= index << 4
        rows.append(row)
    return image.width, image.height, b"".join(rows), used


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source")
    parser.add_argument("--name", required=True)
    parser.add_argument("--width", type=int)
    parser.add_argument("--crop", type=int, nargs=4, metavar=("L", "T", "R", "B"))
    parser.add_argument("--out", required=True)
    parser.add_argument("--append", action="store_true")
    args = parser.parse_args()

    w, h, data, used = convert(Path(args.source), args.width,
                               tuple(args.crop) if args.crop else None)

    body = [
        f"/* {args.name}: {w}x{h}, 4 bpp indexed, transparent index {TRANSPARENT}.\n",
        f" * Generated from {Path(args.source).name} by scripts/gen_asset.py. */\n",
        f"const uint8_t {args.name}_data[] = {{\n",
    ]
    for i in range(0, len(data), 16):
        body.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",\n")
    body.append("};\n\n")
    body.append(
        f"const cads_image_t {args.name} = {{\n"
        f"    .width = {w},\n    .height = {h},\n"
        f"    .transparent = {TRANSPARENT},\n"
        f"    .data = {args.name}_data,\n}};\n\n")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if args.append and out.exists():
        out.write_text(out.read_text() + "".join(body), encoding="utf-8")
    else:
        header = ('/* GENERATED FILE - do not edit. Regenerate with scripts/gen_asset.py */\n\n'
                  '#include "assets/cads_assets.h"\n\n')
        out.write_text(header + "".join(body), encoding="utf-8")

    print(f"{args.name}: {w}x{h}, {len(data)} bytes, palette slots used: {sorted(used)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
