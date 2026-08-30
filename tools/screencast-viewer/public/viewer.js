"use strict";

/* CaDS Zero screencast viewer -- browser side. Talks to bridge.js over a
 * plain WebSocket at /ws: binary messages are either the 41-byte protocol
 * header (see protocol.js) or a raw frame, distinguished by length since
 * the header size (41) and the frame size (width*height/2) never collide
 * in practice; text messages are small JSON status updates from the
 * bridge (board connection state). */

const canvas = document.getElementById("screen");
const ctx = canvas.getContext("2d");
const statusEl = document.getElementById("status");
const dimsEl = document.getElementById("dims");
const fpsEl = document.getElementById("fps");
const framesEl = document.getElementById("frames");

let palette = null;
let frameCount = 0;
let fpsWindow = []; // timestamps of recent frames, for a rolling fps estimate

function setStatus(status, extra) {
  statusEl.textContent =
    status === "connected" ? `board: ${extra && extra.host ? extra.host : ""}:${(extra && extra.port) || ""}` :
    status === "disconnected" ? "board disconnected — retrying…" :
    status === "error" ? `bridge error: ${(extra && extra.message) || "unknown"}` :
    "connecting…";
  statusEl.className = "status status-" + status;
}

function handleHeader(bytes) {
  const header = CadsProtocol.parseHeader(bytes);
  palette = header.palette;
  canvas.width = header.width;
  canvas.height = header.height;
  dimsEl.textContent = `${header.width} × ${header.height} @ ${header.bpp}bpp`;
}

function handleFrame(bytes) {
  if (!palette) return; // header hasn't arrived yet (shouldn't happen, bridge sends it first)
  const rgba = CadsProtocol.unpackFrame(bytes, canvas.width, canvas.height, palette);
  ctx.putImageData(new ImageData(rgba, canvas.width, canvas.height), 0, 0);

  frameCount++;
  framesEl.textContent = `${frameCount} frame${frameCount === 1 ? "" : "s"}`;
  const now = performance.now();
  fpsWindow.push(now);
  fpsWindow = fpsWindow.filter((t) => now - t < 2000);
  if (fpsWindow.length >= 2) {
    const spanS = (fpsWindow[fpsWindow.length - 1] - fpsWindow[0]) / 1000;
    fpsEl.textContent = spanS > 0 ? `${((fpsWindow.length - 1) / spanS).toFixed(1)} fps` : "— fps";
  }
}

function connect() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(`${proto}//${location.host}/ws`);
  ws.binaryType = "arraybuffer";

  ws.addEventListener("open", () => setStatus("connecting"));

  ws.addEventListener("message", (event) => {
    if (typeof event.data === "string") {
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === "status") setStatus(msg.status, msg);
      } catch {
        /* ignore malformed status text, not fatal to the stream */
      }
      return;
    }
    const bytes = new Uint8Array(event.data);
    if (bytes.length === CadsProtocol.HEADER_SIZE) {
      handleHeader(bytes);
    } else {
      handleFrame(bytes);
    }
  });

  ws.addEventListener("close", () => {
    setStatus("disconnected");
    setTimeout(connect, 2000);
  });
  ws.addEventListener("error", () => ws.close());
}

connect();
