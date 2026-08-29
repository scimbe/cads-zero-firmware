"use strict";

const { test } = require("node:test");
const assert = require("node:assert/strict");
const { TcpFramer, HEADER_SIZE } = require("../lib/tcp-framer");

/** Builds a synthetic byte stream: one 41-byte header, then N frames of
 *  the given payloads, each preceded by its 4-byte LE length -- exactly
 *  what the board's cads_screencast_pump() emits on the wire. */
function buildStream(headerByte, framePayloads) {
  const header = Buffer.alloc(HEADER_SIZE, headerByte);
  const parts = [header];
  for (const payload of framePayloads) {
    const lenBuf = Buffer.alloc(4);
    lenBuf.writeUInt32LE(payload.length, 0);
    parts.push(lenBuf, payload);
  }
  return Buffer.concat(parts);
}

test("parses header + single frame delivered in one chunk", () => {
  const frame = Buffer.from([1, 2, 3, 4, 5]);
  const stream = buildStream(0xaa, [frame]);

  const framer = new TcpFramer();
  const units = framer.push(stream);

  assert.equal(units.length, 2);
  assert.equal(units[0].type, "header");
  assert.equal(units[0].data.length, HEADER_SIZE);
  assert.ok(units[0].data.every((b) => b === 0xaa));
  assert.equal(units[1].type, "frame");
  assert.deepEqual([...units[1].data], [1, 2, 3, 4, 5]);
});

test("parses header + multiple frames delivered in one chunk", () => {
  const frameA = Buffer.from([9, 9, 9]);
  const frameB = Buffer.from([7, 7, 7, 7]);
  const stream = buildStream(0x11, [frameA, frameB]);

  const framer = new TcpFramer();
  const units = framer.push(stream);

  assert.equal(units.length, 3); // header, frameA, frameB
  assert.equal(units[0].type, "header");
  assert.equal(units[1].type, "frame");
  assert.deepEqual([...units[1].data], [9, 9, 9]);
  assert.equal(units[2].type, "frame");
  assert.deepEqual([...units[2].data], [7, 7, 7, 7]);
});

test("identical result regardless of how the stream is chunked (byte-at-a-time)", () => {
  const frameA = Buffer.from(Array.from({ length: 20 }, (_, i) => i));
  const frameB = Buffer.from(Array.from({ length: 20 }, (_, i) => 100 + i));
  const stream = buildStream(0x42, [frameA, frameB]);

  // Reference: whole stream in one push.
  const whole = new TcpFramer().push(stream);

  // Same stream, fed one byte at a time -- must produce identical units,
  // since real TCP can and does deliver at arbitrary boundaries.
  const framer = new TcpFramer();
  const collected = [];
  for (let i = 0; i < stream.length; i++) {
    collected.push(...framer.push(stream.subarray(i, i + 1)));
  }

  assert.equal(collected.length, whole.length);
  for (let i = 0; i < whole.length; i++) {
    assert.equal(collected[i].type, whole[i].type);
    assert.deepEqual([...collected[i].data], [...whole[i].data]);
  }
});

test("a frame split exactly across a push boundary still parses correctly", () => {
  const frame = Buffer.from([1, 2, 3, 4, 5, 6, 7, 8]);
  const stream = buildStream(0x00, [frame]);
  const splitPoint = HEADER_SIZE + 4 + 3; // mid-frame, after the length prefix

  const framer = new TcpFramer();
  const first = framer.push(stream.subarray(0, splitPoint));
  const second = framer.push(stream.subarray(splitPoint));

  assert.equal(first.length, 1); // just the header so far
  assert.equal(first[0].type, "header");
  assert.equal(second.length, 1); // the frame completes once the rest arrives
  assert.equal(second[0].type, "frame");
  assert.deepEqual([...second[0].data], [1, 2, 3, 4, 5, 6, 7, 8]);
});

test("board-realistic frame size (480x320 @ 4bpp = 76800 bytes) round-trips exactly", () => {
  const width = 480;
  const height = 320;
  const frameLen = (width / 2) * height;
  const frame = Buffer.alloc(frameLen);
  for (let i = 0; i < frameLen; i++) frame[i] = i & 0xff;
  const stream = buildStream(0x55, [frame]);

  const units = new TcpFramer().push(stream);
  assert.equal(units[1].data.length, frameLen);
  assert.equal(units[1].data[0], 0);
  assert.equal(units[1].data[frameLen - 1], (frameLen - 1) & 0xff);
});
