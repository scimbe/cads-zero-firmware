#!/usr/bin/env node
"use strict";

/* Standalone dev/smoke-test tool: emulates the board's screencast TCP
 * server (port 4244) closely enough to exercise bridge.js end-to-end
 * without real hardware -- NOT a substitute for the real hardware
 * verification the CaDS Zero maintainer will still do, just a way to
 * catch integration bugs (socket wiring, chunking under real TCP,
 * WebSocket relay) that pure unit tests over in-memory buffers can't. */

const net = require("node:net");

const WIDTH = 480;
const HEIGHT = 320;
const STRIDE = WIDTH / 2;
const FRAME_SIZE = STRIDE * HEIGHT;

function buildHeader() {
  const buf = Buffer.alloc(41);
  buf.write("CDZ1", 0, "ascii");
  buf.writeUInt16LE(WIDTH, 4);
  buf.writeUInt16LE(HEIGHT, 6);
  buf[8] = 4;
  // A simple 16-step grey ramp in RGB565 so the viewer shows something
  // visibly structured rather than a flat colour.
  for (let i = 0; i < 16; i++) {
    const v5 = Math.round((i / 15) * 31);
    const v6 = Math.round((i / 15) * 63);
    buf.writeUInt16LE((v5 << 11) | (v6 << 5) | v5, 9 + i * 2);
  }
  return buf;
}

function buildFrame(tick) {
  const buf = Buffer.alloc(FRAME_SIZE);
  // Diagonal moving stripe, same spirit as the real firmware's animated
  // marker -- proves the viewer is reading a genuinely changing buffer.
  const offset = tick % WIDTH;
  for (let y = 0; y < HEIGHT; y++) {
    for (let xByte = 0; xByte < STRIDE; xByte++) {
      const x0 = xByte * 2;
      const x1 = x0 + 1;
      const i0 = (x0 + y + offset) % 16;
      const i1 = (x1 + y + offset) % 16;
      buf[y * STRIDE + xByte] = (i0 << 4) | i1;
    }
  }
  return buf;
}

const server = net.createServer((socket) => {
  console.log("mock board: viewer connected");
  socket.write(buildHeader());

  let tick = 0;
  const timer = setInterval(() => {
    const frame = buildFrame(tick++);
    const len = Buffer.alloc(4);
    len.writeUInt32LE(frame.length, 0);
    socket.write(len);
    socket.write(frame);
  }, 200);

  socket.on("close", () => {
    clearInterval(timer);
    console.log("mock board: viewer disconnected");
  });
  socket.on("error", () => clearInterval(timer));
});

server.listen(4244, () => console.log("mock board screencast listening on :4244"));
