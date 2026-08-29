"use strict";

const { test } = require("node:test");
const assert = require("node:assert/strict");
const CadsProtocol = require("../public/protocol.js");

/** Builds a synthetic 41-byte header exactly as
 *  explorer_screencast_demo.c's cads_screencast_build_header() does. */
function buildHeader(width, height, bpp, palette16 /* array of 16 u16 RGB565 */) {
  const buf = Buffer.alloc(41);
  buf.write("CDZ1", 0, "ascii");
  buf.writeUInt16LE(width, 4);
  buf.writeUInt16LE(height, 6);
  buf[8] = bpp;
  for (let i = 0; i < 16; i++) buf.writeUInt16LE(palette16[i], 9 + i * 2);
  return buf;
}

const SOME_PALETTE = [
  0x0000, 0xffff, 0x2049, 0xb618, 0x9d67, 0x18e3, 0x8410, 0xc618, 0xf800, 0xfd20, 0x0451, 0x1024, 0xc618,
  0xf81f, 0x8c92, 0x0000,
];

test("parseHeader extracts magic/width/height/bpp/palette correctly", () => {
  const header = buildHeader(480, 320, 4, SOME_PALETTE);
  const parsed = CadsProtocol.parseHeader(header);

  assert.equal(parsed.width, 480);
  assert.equal(parsed.height, 320);
  assert.equal(parsed.bpp, 4);
  assert.equal(parsed.palette.length, 16);
  for (let i = 0; i < 16; i++) assert.equal(parsed.palette[i], SOME_PALETTE[i]);
});

test("parseHeader rejects wrong length", () => {
  assert.throws(() => CadsProtocol.parseHeader(Buffer.alloc(40)), /41 bytes/);
  assert.throws(() => CadsProtocol.parseHeader(Buffer.alloc(42)), /41 bytes/);
});

test("parseHeader rejects wrong magic", () => {
  const header = buildHeader(480, 320, 4, SOME_PALETTE);
  header.write("XXXX", 0, "ascii");
  assert.throws(() => CadsProtocol.parseHeader(header), /bad magic/);
});

test("parseHeader rejects non-4 bpp", () => {
  const header = buildHeader(480, 320, 8, SOME_PALETTE);
  assert.throws(() => CadsProtocol.parseHeader(header), /unsupported bpp/);
});

test("rgb565to888: pure black, pure white, and a known mid colour", () => {
  assert.deepEqual(CadsProtocol.rgb565to888(0x0000), [0, 0, 0]);
  assert.deepEqual(CadsProtocol.rgb565to888(0xffff), [255, 255, 255]);
  // CaDS brand blue #204C86 quantized to RGB565 and back should land close
  // to the original (bit-replication, not truncation, so within a few LSBs).
  const r5 = Math.round((0x20 / 255) * 31);
  const g6 = Math.round((0x4c / 255) * 63);
  const b5 = Math.round((0x86 / 255) * 31);
  const v = (r5 << 11) | (g6 << 5) | b5;
  const [r, g, b] = CadsProtocol.rgb565to888(v);
  assert.ok(Math.abs(r - 0x20) <= 8);
  assert.ok(Math.abs(g - 0x4c) <= 4);
  assert.ok(Math.abs(b - 0x86) <= 8);
});

test("unpackFrame: high nibble is the FIRST (even-x) pixel, matching gui/canvas.c", () => {
  // 4x1 image, 1 byte per pair -> stride 2 bytes. Byte0 encodes pixels
  // (x=0,1), byte1 encodes pixels (x=2,3). Palette index N -> plain grey
  // (N*16, N*16, N*16) via a palette we control fully, so the test asserts
  // exact values, not just "close" ones.
  const width = 4;
  const height = 1;
  const palette = new Uint16Array(16);
  for (let i = 0; i < 16; i++) {
    // 5-5-5-ish grey packed into RGB565 so rgb565to888 gives i*8ish per
    // channel -- exact value doesn't matter, only that each index maps to
    // a DISTINCT, recoverable colour so pixel order is unambiguous.
    palette[i] = (i << 11) | (i << 6) | i; // distinct per index
  }
  // byte0: high nibble = index 3 (x=0), low nibble = index 5 (x=1)
  // byte1: high nibble = index 9 (x=2), low nibble = index 12 (x=3)
  const frame = Buffer.from([(3 << 4) | 5, (9 << 4) | 12]);

  const rgba = CadsProtocol.unpackFrame(frame, width, height, palette);
  assert.equal(rgba.length, width * height * 4);

  const expectedRgb = [3, 5, 9, 12].map((idx) => CadsProtocol.rgb565to888(palette[idx]));
  for (let x = 0; x < width; x++) {
    const [r, g, b] = expectedRgb[x];
    assert.equal(rgba[x * 4 + 0], r, `pixel ${x} red`);
    assert.equal(rgba[x * 4 + 1], g, `pixel ${x} green`);
    assert.equal(rgba[x * 4 + 2], b, `pixel ${x} blue`);
    assert.equal(rgba[x * 4 + 3], 255, `pixel ${x} alpha`);
  }
});

test("unpackFrame: multi-row image indexes rows by stride, not width", () => {
  // 4x2 image: row0 = [byte0, byte1], row1 = [byte2, byte3], stride=2.
  const width = 4;
  const height = 2;
  const palette = new Uint16Array(16);
  for (let i = 0; i < 16; i++) palette[i] = (i << 11) | (i << 6) | i;

  // row0: indices 0,1,2,3 ; row1: indices 4,5,6,7
  const frame = Buffer.from([(0 << 4) | 1, (2 << 4) | 3, (4 << 4) | 5, (6 << 4) | 7]);
  const rgba = CadsProtocol.unpackFrame(frame, width, height, palette);

  const px = (x, y) => {
    const o = (y * width + x) * 4;
    return [rgba[o], rgba[o + 1], rgba[o + 2]];
  };
  // spot-check corners and row boundary
  assert.deepEqual(px(0, 0), CadsProtocol.rgb565to888(palette[0]));
  assert.deepEqual(px(3, 0), CadsProtocol.rgb565to888(palette[3]));
  assert.deepEqual(px(0, 1), CadsProtocol.rgb565to888(palette[4]));
  assert.deepEqual(px(3, 1), CadsProtocol.rgb565to888(palette[7]));
});

test("unpackFrame rejects a frame that doesn't match width*height/2", () => {
  const palette = new Uint16Array(16);
  assert.throws(() => CadsProtocol.unpackFrame(Buffer.alloc(10), 480, 320, palette), /!= expected/);
});

test("unpackFrame at real board dimensions (480x320) produces the right buffer size", () => {
  const width = 480;
  const height = 320;
  const stride = width / 2;
  const frame = Buffer.alloc(stride * height, 0x12); // every pixel index 1 (low nibble of 0x12=2, high=1)
  const palette = new Uint16Array(16);
  for (let i = 0; i < 16; i++) palette[i] = (i << 11) | (i << 6) | i;

  const rgba = CadsProtocol.unpackFrame(frame, width, height, palette);
  assert.equal(rgba.length, width * height * 4);
  // spot check a pixel deep in the middle of the buffer, not just the edges
  const midX = 240;
  const midY = 160;
  const o = (midY * width + midX) * 4;
  const expected = CadsProtocol.rgb565to888(palette[midX % 2 === 0 ? 1 : 2]);
  assert.deepEqual([rgba[o], rgba[o + 1], rgba[o + 2]], expected);
});
