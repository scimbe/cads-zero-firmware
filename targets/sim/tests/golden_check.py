#!/usr/bin/env python3
"""CaDS Zero - golden-image capture and comparison for the host simulator.

The simulator's `--screenshot` mode (targets/sim/hal_sim.c, "THE SCREENSHOT
FIRES ON QUIESCENCE") saves the first frame the panel goes untouched on, as a
24 bpp BMP - SDL writes no other format, and README.md says so explicitly.
This script runs the simulator, decodes that BMP itself (no image library:
the format SDL_SaveBMP actually emits for our framebuffer is one fixed,
well-documented shape - see `read_bmp_rgb24`), and either:

  * `compare` - reference the result against a golden PNG, byte for byte, and
    on any mismatch write a diff PNG next to it; or
  * `update`  - overwrite the golden PNG with what the simulator draws right
    now, which is the whole point of a golden test being able to move on
    when a UI change is intentional rather than a regression.

Why PNG and not another BMP: `docs/reference/module-layout.md` prefers small,
lossless, reviewable assets, and a 480x320 24 bpp BMP is 460,854 bytes with
nothing to show for it - every one of these screens is drawn from the
canvas's fixed palette (`modules toolbox` / `gui/canvas.c`, 4 bpp indexed),
so it never has more than a few dozen distinct colours. PNG's palette
(indexed-colour) mode plus real DEFLATE compression turns that into a few
kilobytes.

Why hand-write the PNG codec instead of vendoring a C library: this project
already has a hard, enforced dependency on Python 3 for exactly this kind of
host-side tooling (`find_package(Python3 ... REQUIRED)` in the top-level
CMakeLists.txt; see also scripts/board_test.py, scripts/gen_font.py). Python's
standard library ships `zlib` - the real, battle-tested DEFLATE/zlib codec,
not a hand-rolled one - so the PNG layer here only has to assemble chunks
around it (IHDR/PLTE/IDAT/IEND with their CRC-32s) rather than implement
compression. That is smaller and less risky than either vendoring a new C PNG
library for one debug feature (the exact reasoning targets/sim/README.md
already gives for staying with BMP inside the simulator itself) or
hand-rolling stored (uncompressed) DEFLATE blocks, which would produce PNGs
*larger* than the BMP they replace and defeat the point.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

CADS_DISPLAY_WIDTH = 480
CADS_DISPLAY_HEIGHT = 320

# A differing pixel is painted at full, unmistakable saturation; a matching
# one is dimmed to a third of its value. Every real pixel in these images
# comes from the canvas's palette, so no un-dimmed channel can be at 255
# without being a marked difference - see make_diff_image().
DIFF_MARK = (255, 0, 0)


# --- BMP (read-only; this is what SDL_SaveBMP writes for our framebuffer) ---


def read_bmp_rgb24(path: Path) -> tuple[int, int, bytes]:
    """Decode the specific BMP shape SDL_SaveBMP produces here.

    Verified empirically against the simulator's own output (see
    targets/sim/golden/README.md): BITMAPFILEHEADER (14 bytes) +
    BITMAPINFOHEADER (40 bytes), 24 bpp, BI_RGB (no compression, no bitfield
    masks). The simulator widens its RGB565 framebuffer to 24 bpp itself,
    by bit replication, before SDL sees it - SDL's own conversion rounds
    differently between SDL builds (targets/sim/golden/README.md). Rows are
    BGR, one byte per channel, stored bottom-up per the BMP convention
    (positive height), padded to a 4-byte boundary; 480*3 = 1440 is already
    a multiple of 4, so in practice there is no padding to strip, but the
    padding math below does not assume that.

    Returns (width, height, rgb_bytes) with rgb_bytes top-down, row-major,
    3 bytes per pixel, R first - the same layout encode_png() expects and
    the diff image is built in.
    """
    data = path.read_bytes()

    if len(data) < 54 or data[0:2] != b"BM":
        raise ValueError(f"{path}: not a BMP (bad magic)")

    (offbits,) = struct.unpack_from("<I", data, 10)
    (header_size, width, height, planes, bpp, compression) = struct.unpack_from(
        "<IiiHHI", data, 14
    )

    if header_size < 40:
        raise ValueError(f"{path}: BMP header too old ({header_size} bytes)")
    if bpp != 24 or compression != 0:
        raise ValueError(
            f"{path}: expected 24 bpp BI_RGB, got {bpp} bpp compression={compression} "
            "- SDL_SaveBMP's output shape changed; read_bmp_rgb24() needs updating"
        )
    if planes != 1:
        raise ValueError(f"{path}: unexpected plane count {planes}")

    top_down = height < 0
    height = abs(height)
    row_stride = ((width * 3 + 3) // 4) * 4

    needed = offbits + row_stride * height
    if len(data) < needed:
        raise ValueError(f"{path}: truncated ({len(data)} bytes, need {needed})")

    rows = []
    for row in range(height):
        start = offbits + row * row_stride
        bgr = data[start : start + width * 3]
        # BMP stores BGR; flip each triplet to RGB.
        rgb = bytearray(width * 3)
        rgb[0::3] = bgr[2::3]
        rgb[1::3] = bgr[1::3]
        rgb[2::3] = bgr[0::3]
        rows.append(bytes(rgb))

    if not top_down:
        rows.reverse()  # BMP bottom-up -> our top-down convention

    return width, height, b"".join(rows)


# --- PNG (write always; read only for golden files this same script wrote) --


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def encode_png(width: int, height: int, rgb: bytes) -> bytes:
    """RGB bytes (top-down, row-major, 3 bytes/pixel) -> a valid PNG file.

    Indexed colour (PNG colour type 3) when the image has at most 256 unique
    colours - true for every screen this simulator can draw, since the whole
    canvas is 4 bpp indexed - falling back to 8-bit truecolour (colour type
    2) otherwise so this function stays correct for an image it was not
    designed around rather than silently mis-encoding one. No per-row
    filtering (filter type 0, None, throughout): these images are large flat
    regions, so real DEFLATE (via zlib.compress) already does the work a
    predictive filter exists to help with, and skipping it keeps the encoder
    to one code path.
    """
    pixels = [rgb[i : i + 3] for i in range(0, len(rgb), 3)]
    palette = sorted(set(pixels))

    signature = b"\x89PNG\r\n\x1a\n"

    if len(palette) <= 256:
        index_of = {colour: i for i, colour in enumerate(palette)}
        raw = bytearray()
        for y in range(height):
            raw.append(0)  # filter type: None
            row = pixels[y * width : (y + 1) * width]
            raw.extend(index_of[c] for c in row)
        ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
        plte = b"".join(palette)
        chunks = (
            signature
            + _chunk(b"IHDR", ihdr)
            + _chunk(b"PLTE", plte)
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + _chunk(b"IEND", b"")
        )
    else:
        raw = bytearray()
        for y in range(height):
            raw.append(0)
            raw.extend(rgb[y * width * 3 : (y + 1) * width * 3])
        ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
        chunks = (
            signature
            + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + _chunk(b"IEND", b"")
        )

    return chunks


def decode_png(path: Path) -> tuple[int, int, bytes]:
    """Inverse of encode_png(). Only needs to understand what that function
    writes (indexed or truecolour, 8-bit, filter type None, single IDAT
    stream split across any number of IDAT chunks) - it is not a general PNG
    decoder, and raises rather than guessing on anything else, the same
    discipline read_bmp_rgb24() applies to SDL's BMP output.
    """
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG (bad signature)")

    pos = 8
    width = height = bit_depth = colour_type = None
    palette: list[bytes] = []
    idat = bytearray()

    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        tag = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        pos += 8 + length + 4  # skip the trailing CRC

        if tag == b"IHDR":
            width, height, bit_depth, colour_type, comp, filt, interlace = (
                struct.unpack(">IIBBBBB", payload)
            )
            if bit_depth != 8 or comp != 0 or filt != 0 or interlace != 0:
                raise ValueError(f"{path}: unsupported IHDR {payload!r}")
            if colour_type not in (2, 3):
                raise ValueError(f"{path}: unsupported colour type {colour_type}")
        elif tag == b"PLTE":
            palette = [payload[i : i + 3] for i in range(0, len(payload), 3)]
        elif tag == b"IDAT":
            idat.extend(payload)
        elif tag == b"IEND":
            break

    if width is None:
        raise ValueError(f"{path}: no IHDR chunk")

    raw = zlib.decompress(bytes(idat))
    bytes_per_pixel = 1 if colour_type == 3 else 3
    stride = 1 + width * bytes_per_pixel

    rgb = bytearray(width * height * 3)
    for y in range(height):
        row_start = y * stride
        filter_type = raw[row_start]
        if filter_type != 0:
            raise ValueError(
                f"{path}: row filter {filter_type} not supported "
                "(this decoder only reads what encode_png() writes)"
            )
        row = raw[row_start + 1 : row_start + stride]
        out = y * width * 3
        if colour_type == 3:
            for x in range(width):
                rgb[out + x * 3 : out + x * 3 + 3] = palette[row[x]]
        else:
            rgb[out : out + width * 3] = row

    return width, height, bytes(rgb)


# --- diff image ---------------------------------------------------------------


def make_diff_image(width: int, height: int, golden: bytes, actual: bytes) -> bytes:
    """Golden vs. actual, same shape: matching pixels dimmed to a third of
    their brightness so the failing frame's composition stays legible, every
    differing pixel painted solid red. See the module docstring / README for
    why this needs no fuzzy threshold: the comparison it visualises is exact.
    """
    out = bytearray(width * height * 3)
    for i in range(0, len(actual), 3):
        if golden[i : i + 3] == actual[i : i + 3]:
            out[i] = actual[i] // 3
            out[i + 1] = actual[i + 1] // 3
            out[i + 2] = actual[i + 2] // 3
        else:
            out[i] = DIFF_MARK[0]
            out[i + 1] = DIFF_MARK[1]
            out[i + 2] = DIFF_MARK[2]
    return bytes(out)


# --- driving the simulator ------------------------------------------------


def capture(sim_exe: Path, idle_ms: int, timeout_ms: int) -> tuple[int, int, bytes]:
    """Run the simulator to a quiescent screenshot and decode it.

    The BMP itself always goes to a throwaway temp directory, never next to
    the golden PNGs or wherever a caller's other output lands - a screenshot
    is an intermediate artifact of this function, not a result either
    command produces, and it was a real bug the first time around: `update`
    briefly wrote it straight into targets/sim/golden/ and it almost got
    committed alongside the PNG it was captured for.

    stdin is /dev/null: every golden scene this script knows about is
    reachable with no console input (see README.md - the "colour bar" self
    test pattern is not reached through explorer.c's `p` command, which has
    no equivalent value for it, but by giving `--screenshot-idle` a value
    longer than the boot splash's fixed 1.5 s hold so the earliest quiescent
    moment is *after* the self test's own final frame instead of during the
    splash). A command run under a timeout is this project's own rule
    (scripts/board_test.py's docstring); the simulator already enforces
    --screenshot-timeout internally, so the wrapper timeout here is only the
    backstop for a hang in the process launch itself.
    """
    with tempfile.TemporaryDirectory(prefix="cads-golden-") as tmp_dir:
        bmp_path = Path(tmp_dir) / "capture.bmp"

        result = subprocess.run(
            [
                str(sim_exe),
                "--screenshot",
                str(bmp_path),
                "--screenshot-idle",
                str(idle_ms),
                "--screenshot-timeout",
                str(timeout_ms),
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=(timeout_ms / 1000.0) + 10.0,
            text=True,
        )

        if result.returncode != 0 or not bmp_path.exists():
            sys.stderr.write(result.stdout)
            raise SystemExit(
                f"golden_check: {sim_exe} did not produce a screenshot "
                f"(exit {result.returncode})"
            )

        return read_bmp_rgb24(bmp_path)


# --- commands --------------------------------------------------------------


def cmd_compare(args: argparse.Namespace) -> int:
    golden_path = Path(args.golden)
    if not golden_path.exists():
        raise SystemExit(
            f"golden_check: {golden_path} does not exist - run with 'update' "
            "first to create it (see targets/sim/golden/README.md)"
        )

    width, height, actual = capture(Path(args.sim), args.idle, args.timeout)
    gwidth, gheight, golden = decode_png(golden_path)

    if (width, height) != (gwidth, gheight):
        print(
            f"not ok - {args.name}: size mismatch, golden is {gwidth}x{gheight}, "
            f"captured {width}x{height}"
        )
        return 1

    if actual == golden:
        print(f"ok - {args.name}: matches {golden_path}")
        return 0

    diff_path = Path(args.diff)
    diff_path.parent.mkdir(parents=True, exist_ok=True)
    diff_path.write_bytes(encode_png(width, height, make_diff_image(width, height, golden, actual)))

    differing = sum(1 for i in range(0, len(actual), 3) if actual[i : i + 3] != golden[i : i + 3])
    print(
        f"not ok - {args.name}: {differing}/{width * height} pixels differ from "
        f"{golden_path}\n  diff image: {diff_path}"
    )
    return 1


def cmd_update(args: argparse.Namespace) -> int:
    golden_path = Path(args.golden)
    golden_path.parent.mkdir(parents=True, exist_ok=True)

    width, height, actual = capture(Path(args.sim), args.idle, args.timeout)
    golden_path.write_bytes(encode_png(width, height, actual))

    print(f"ok - {args.name}: wrote {golden_path} ({golden_path.stat().st_size} bytes)")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--sim", required=True, help="path to the cads-zero-sim executable")
    common.add_argument("--golden", required=True, help="path to the reference PNG")
    common.add_argument(
        "--name", default=None, help="label for TAP-style output (default: golden PNG stem)"
    )
    common.add_argument(
        "--idle",
        type=int,
        default=250,
        help="--screenshot-idle passed to the simulator, ms (default: 250)",
    )
    common.add_argument(
        "--timeout",
        type=int,
        default=10000,
        help="--screenshot-timeout passed to the simulator, ms (default: 10000)",
    )

    compare = sub.add_parser("compare", parents=[common], help="compare against the golden PNG")
    compare.add_argument(
        "--diff", required=True, help="where to write a diff PNG if the comparison fails"
    )
    compare.set_defaults(func=cmd_compare)

    update = sub.add_parser("update", parents=[common], help="regenerate the golden PNG")
    update.set_defaults(func=cmd_update)

    args = parser.parse_args(argv)
    if args.name is None:
        args.name = Path(args.golden).stem

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
