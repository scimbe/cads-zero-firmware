#!/usr/bin/env node
"use strict";

/*
 * CaDS Zero screencast bridge.
 *
 * A dependency-free Node bridge that speaks the CaDS Zero board's raw TCP
 * screencast protocol (see apps/bringup/explorer_screencast_demo.c in
 * scimbe/cads-zero for the authoritative wire format) and re-serves it to
 * any number of browser tabs over WebSocket, plus serves the viewer's own
 * static page. One process, one port, `node bridge.js <board-host> [port]`.
 *
 * Wire protocol from the board (unchanged here, just re-framed):
 *   Once per TCP connection: a 41-byte header --
 *     bytes 0-3   magic "CDZ1"
 *     bytes 4-5   width,  u16 LE
 *     bytes 6-7   height, u16 LE
 *     byte  8     bits per pixel (4)
 *     bytes 9-40  palette, 16 x RGB565 LE
 *   Then forever: frame length (u32 LE) + that many bytes of packed 4bpp
 *   pixels (row-major, two pixels per byte, high nibble first).
 *
 * This bridge forwards the header and each frame's raw bytes to every
 * connected browser as one binary WebSocket message each -- unpacking the
 * palette into pixels is the browser's job (viewer.js), same "protocol is
 * documented, not pre-interpreted" spirit as the board side.
 */

const net = require("node:net");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { TcpFramer } = require("./lib/tcp-framer");

const BOARD_PORT = 4244;
const RECONNECT_DELAY_MS = 3000;
const WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

const boardHost = process.argv[2];
const httpPort = Number(process.argv[3]) || 8642;

if (!boardHost) {
  console.error("usage: node bridge.js <board-host-or-ip> [http-port]");
  process.exit(1);
}

// ---- static file serving -------------------------------------------------

const PUBLIC_DIR = path.join(__dirname, "public");
const MIME = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css" };

function serveStatic(req, res) {
  let rel = req.url === "/" ? "/index.html" : req.url;
  const filePath = path.join(PUBLIC_DIR, path.normalize(rel).replace(/^(\.\.[/\\])+/, ""));
  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { "content-type": "text/plain" });
      res.end("not found");
      return;
    }
    const ext = path.extname(filePath);
    res.writeHead(200, { "content-type": MIME[ext] || "application/octet-stream" });
    res.end(data);
  });
}

// ---- minimal WebSocket server (RFC 6455, server frames need no mask) -----

const wsClients = new Set();

function acceptKey(clientKey) {
  return crypto.createHash("sha1").update(clientKey + WS_GUID).digest("base64");
}

/** Encodes one WebSocket frame. opcode 0x1=text, 0x2=binary. No masking
 *  (server->client frames are sent unmasked per RFC 6455). */
function encodeFrame(payload, opcode) {
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.from([0x80 | opcode, len]);
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode;
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 127;
    header.writeUInt32BE(0, 2);
    header.writeUInt32BE(len, 6);
  }
  return Buffer.concat([header, payload]);
}

function wsSendBinary(socket, buf) {
  try {
    socket.write(encodeFrame(buf, 0x2));
  } catch {
    /* socket already gone; the close handler cleans up wsClients */
  }
}

function wsSendText(socket, str) {
  try {
    socket.write(encodeFrame(Buffer.from(str, "utf8"), 0x1));
  } catch {
    /* ignore */
  }
}

function broadcastBinary(buf) {
  for (const client of wsClients) wsSendBinary(client, buf);
}

function broadcastStatus(status, extra) {
  const msg = JSON.stringify({ type: "status", status, ...extra });
  for (const client of wsClients) wsSendText(client, msg);
}

function handleUpgrade(req, socket) {
  const key = req.headers["sec-websocket-key"];
  if (!key) {
    socket.destroy();
    return;
  }
  const responseHeaders = [
    "HTTP/1.1 101 Switching Protocols",
    "Upgrade: websocket",
    "Connection: Upgrade",
    `Sec-WebSocket-Accept: ${acceptKey(key)}`,
    "\r\n",
  ];
  socket.write(responseHeaders.join("\r\n"));

  wsClients.add(socket);
  broadcastStatus.lastState && wsSendText(socket, JSON.stringify(broadcastStatus.lastState));
  if (cachedHeader) wsSendBinary(socket, cachedHeader);

  // Browsers never need to send us anything meaningful; drain frames so the
  // socket doesn't back up, and let the raw close/reset drop the client.
  socket.on("data", () => {});
  socket.on("close", () => wsClients.delete(socket));
  socket.on("error", () => wsClients.delete(socket));
}

// ---- board TCP client: parses the exact wire protocol above --------------

let cachedHeader = null; // last 41-byte header, replayed to newly-joined viewers

function connectToBoard() {
  const socket = net.connect(BOARD_PORT, boardHost);
  const framer = new TcpFramer();

  const setStatus = (status, extra = {}) => {
    broadcastStatus.lastState = { type: "status", status, ...extra };
    broadcastStatus(status, extra);
  };

  socket.on("connect", () => {
    console.log(`connected to board screencast at ${boardHost}:${BOARD_PORT}`);
    setStatus("connected", { host: boardHost, port: BOARD_PORT });
  });

  socket.on("data", (chunk) => {
    let units;
    try {
      units = framer.push(chunk);
    } catch (err) {
      console.error(`framer error: ${err.message}, closing`);
      socket.destroy();
      return;
    }
    for (const unit of units) {
      if (unit.type === "header") {
        if (unit.data.toString("ascii", 0, 4) !== "CDZ1") {
          console.error("bad magic in screencast header, closing");
          socket.destroy();
          return;
        }
        cachedHeader = unit.data;
      }
      broadcastBinary(unit.data);
    }
  });

  socket.on("close", () => {
    console.log("board connection closed, retrying in " + RECONNECT_DELAY_MS + "ms");
    setStatus("disconnected", { host: boardHost, port: BOARD_PORT });
    cachedHeader = null;
    setTimeout(connectToBoard, RECONNECT_DELAY_MS);
  });

  socket.on("error", (err) => {
    console.log(`board connect error: ${err.message}, retrying in ${RECONNECT_DELAY_MS}ms`);
    setStatus("error", { message: err.message });
  });
}

// ---- wire it up ------------------------------------------------------------

const server = http.createServer(serveStatic);
server.on("upgrade", (req, socket) => {
  if (req.url === "/ws") handleUpgrade(req, socket);
  else socket.destroy();
});

server.listen(httpPort, () => {
  console.log(`CaDS Zero screencast viewer: http://localhost:${httpPort}`);
  console.log(`bridging to board at ${boardHost}:${BOARD_PORT}`);
});

connectToBoard();
