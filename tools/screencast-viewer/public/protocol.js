/*
 * CaDS Zero screencast wire-protocol parsing -- pure, no DOM/WebSocket.
 * Kept isolated from viewer.js so the exact same code can be exercised by
 * Node-side tests against synthetic frames (see tests/protocol.test.js),
 * since there is no network path from this environment to a live board.
 *
 * Matches apps/bringup/explorer_screencast_demo.c (scimbe/cads-zero) byte
 * for byte:
 *   header (41 bytes): magic "CDZ1" (4) + width u16LE (2) + height u16LE (2)
 *     + bpp (1, always 4) + 16 x RGB565LE palette (32)
 *   frame: packed 4bpp pixels, row-major, two pixels per byte, HIGH nibble
 *     first (gui/canvas.h's cads_canvas_draw_bitmap4 layout, read straight
 *     out of cads_canvas_buffer()). Row stride is exactly width/2 bytes --
 *     the board's CADS_CANVAS_WIDTH (480) is even, so no row padding bits.
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.CadsProtocol = factory();
  }
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  const HEADER_SIZE = 41;
  const PALETTE_SIZE = 16;
  const MAGIC = "CDZ1";

  /** parseHeader(buf: Uint8Array|Buffer) -> {width, height, bpp, palette: Uint16Array(16)} */
  function parseHeader(buf) {
    if (buf.length !== HEADER_SIZE) {
      throw new Error(`header must be exactly ${HEADER_SIZE} bytes, got ${buf.length}`);
    }
    const magic = String.fromCharCode(buf[0], buf[1], buf[2], buf[3]);
    if (magic !== MAGIC) {
      throw new Error(`bad magic "${magic}", expected "${MAGIC}"`);
    }
    const width = buf[4] | (buf[5] << 8);
    const height = buf[6] | (buf[7] << 8);
    const bpp = buf[8];
    if (bpp !== 4) {
      throw new Error(`unsupported bpp ${bpp}, this viewer only understands 4bpp indexed frames`);
    }
    const palette = new Uint16Array(PALETTE_SIZE);
    for (let i = 0; i < PALETTE_SIZE; i++) {
      const lo = buf[9 + i * 2];
      const hi = buf[9 + i * 2 + 1];
      palette[i] = lo | (hi << 8);
    }
    return { width, height, bpp, palette };
  }

  /** rgb565to888(v: u16) -> [r, g, b] (0..255 each, bit-replicated for accuracy) */
  function rgb565to888(v) {
    const r5 = (v >> 11) & 0x1f;
    const g6 = (v >> 5) & 0x3f;
    const b5 = v & 0x1f;
    const r = (r5 << 3) | (r5 >> 2);
    const g = (g6 << 2) | (g6 >> 4);
    const b = (b5 << 3) | (b5 >> 2);
    return [r, g, b];
  }

  /**
   * unpackFrame(frame, width, height, palette) -> Uint8ClampedArray RGBA,
   * length width*height*4, ready for `new ImageData(arr, width, height)`.
   */
  function unpackFrame(frame, width, height, palette) {
    const stride = width >> 1; // two pixels per byte
    const expected = stride * height;
    if (frame.length !== expected) {
      throw new Error(`frame length ${frame.length} != expected ${expected} (stride ${stride} x height ${height})`);
    }
    // Precompute RGB for all 16 palette entries once per frame -- cheap
    // (16 entries), avoids recomputing the same bit math per pixel.
    const rgb = new Array(PALETTE_SIZE);
    for (let i = 0; i < PALETTE_SIZE; i++) rgb[i] = rgb565to888(palette[i]);

    const out = new Uint8ClampedArray(width * height * 4);
    let outIdx = 0;
    for (let y = 0; y < height; y++) {
      const rowStart = y * stride;
      for (let x = 0; x < width; x++) {
        const byteIdx = rowStart + (x >> 1);
        const byte = frame[byteIdx];
        // high nibble first: even x -> high nibble, odd x -> low nibble
        const index = (x & 1) === 0 ? (byte >> 4) & 0x0f : byte & 0x0f;
        const [r, g, b] = rgb[index];
        out[outIdx++] = r;
        out[outIdx++] = g;
        out[outIdx++] = b;
        out[outIdx++] = 255;
      }
    }
    return out;
  }

  return { HEADER_SIZE, PALETTE_SIZE, MAGIC, parseHeader, rgb565to888, unpackFrame };
});
