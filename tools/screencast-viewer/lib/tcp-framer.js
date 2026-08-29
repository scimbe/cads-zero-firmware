"use strict";

/*
 * Stateful parser for the CaDS Zero screencast TCP byte stream: once per
 * connection a fixed 41-byte header, then forever (4-byte LE length +
 * that many bytes) frames. Kept separate from bridge.js's socket-handling
 * so this exact state machine can be exercised by tests/framer.test.js
 * against synthetic byte streams (arbitrarily chunked, since real TCP
 * delivers chunks at whatever boundaries the network happens to produce --
 * a correct framer must not assume chunk boundaries align with protocol
 * boundaries).
 */

const HEADER_SIZE = 41;

class TcpFramer {
  constructor() {
    this._buf = Buffer.alloc(0);
    this._phase = "header"; // "header" | "length" | "body"
    this._frameLength = 0;
  }

  /**
   * push(chunk: Buffer) -> Array<{type: "header"|"frame", data: Buffer}>
   * Feed one chunk of bytes (any size, any alignment); returns zero or more
   * fully-parsed units. Each returned `data` is a fresh copy, safe to keep
   * after the next push() call reuses internal buffers.
   */
  push(chunk) {
    this._buf = this._buf.length ? Buffer.concat([this._buf, chunk]) : Buffer.from(chunk);
    const out = [];

    for (;;) {
      if (this._phase === "header") {
        if (this._buf.length < HEADER_SIZE) break;
        out.push({ type: "header", data: Buffer.from(this._buf.subarray(0, HEADER_SIZE)) });
        this._buf = this._buf.subarray(HEADER_SIZE);
        this._phase = "length";
        continue;
      }
      if (this._phase === "length") {
        if (this._buf.length < 4) break;
        this._frameLength = this._buf.readUInt32LE(0);
        this._buf = this._buf.subarray(4);
        this._phase = "body";
        continue;
      }
      // phase === "body"
      if (this._buf.length < this._frameLength) break;
      out.push({ type: "frame", data: Buffer.from(this._buf.subarray(0, this._frameLength)) });
      this._buf = this._buf.subarray(this._frameLength);
      this._phase = "length";
    }

    return out;
  }
}

module.exports = { TcpFramer, HEADER_SIZE };
