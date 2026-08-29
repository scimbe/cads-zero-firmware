# CaDS Zero screencast viewer

A small, dependency-free web app that turns CaDS Zero's screen-streaming
TCP protocol (see `apps/bringup/explorer_screencast_demo.c`, roadmap item
"[M5] Screen streaming: framebuffer to a host viewer over TCP") into a live
remote-desktop-style view of the board's screen in a browser — the
reference host-side viewer that milestone's own roadmap note flagged as
never having been written.

## What it is

- `bridge.js` — a Node process (no npm dependencies) that connects to the
  board's screencast TCP listener (`<board-ip>:4244`), parses the wire
  protocol, and re-serves it to any number of browser tabs over a plain
  WebSocket at `/ws`. Also serves the viewer page itself, so the whole
  thing is one process on one port.
- `public/` — the browser page: `protocol.js` (pure, DOM-free wire-protocol
  parsing — the part that has to be byte-exact), `viewer.js` (WebSocket
  glue + canvas rendering), `index.html`/`style.css` (the page itself, in
  CaDS's own brand colours: `#204C86` / `#B5C4D8` / `#9CB33B`).
- `lib/tcp-framer.js` — the stateful length-prefixed framing parser,
  factored out of `bridge.js` so it's independently testable against
  synthetic, arbitrarily-chunked byte streams (real TCP doesn't deliver at
  protocol-boundary-aligned chunk sizes, so a framer that only works for
  one chunking pattern is not actually correct).
- `tests/` — `node --test` unit tests for the framer and the protocol
  parser/unpacker, plus `mock-board.js`, a standalone TCP server that
  emulates the board's screencast protocol closely enough to smoke-test
  the whole pipeline (TCP → framer → WebSocket → browser unpacking)
  without real hardware.

## Running it

```bash
node bridge.js <board-ip-or-hostname> [http-port]   # default port 8642
```

Then open `http://localhost:8642` in a browser on the Mac. The board needs
its screencast listener running first — from the board's console/serial
CLI, `S <seconds>` (0 = run until a byte arrives on the console). Multiple
browser tabs can watch the same stream at once even though the board only
accepts one TCP viewer — the bridge fans the single board connection out
to every connected browser.

## Wire protocol (ground truth: the board's own source)

Once per TCP connection, a 41-byte header: magic `"CDZ1"` (4 bytes),
width/height as `u16` little-endian (2+2 bytes), bits-per-pixel = `4`
(1 byte, sent on the wire — don't hardcode past it), then a 16-entry
RGB565-little-endian palette (32 bytes). Then forever: a `u32` little-endian
frame length, followed by that many bytes of packed 4bpp framebuffer,
row-major, two pixels per byte, **high nibble first** (verified directly
against `gui/canvas.c`'s `cads_canvas_set_pixel`/`get_pixel`/
`draw_bitmap4`/palette-unpack — all four sites agree; the doc comments in
`gui/canvas.h` and `explorer_screencast_demo.c` say the same thing).

## Verification status

- **Protocol logic**: unit-tested against synthetic data built to the exact
  byte layout above (`tests/protocol.test.js`, `tests/framer.test.js`) —
  14/14 passing, including a test that pins down the nibble order
  explicitly (this was the one detail worth double-checking, since an
  initial verbal description from the firmware maintainer said the
  opposite — the actual `gui/canvas.c` source settled it).
- **End-to-end pipeline**: smoke-tested against `tests/mock-board.js` (a
  fake TCP server emitting the same wire format, run locally) — real
  socket I/O, real WebSocket handshake/framing (hand-rolled, no `ws`
  dependency, so this exercise mattered), real browser-side unpacking.
  8 consecutive animated frames parsed correctly.
- **NOT yet verified against real hardware.** This environment has no
  network path to the board's bench segment. Per the CaDS Zero maintainer:
  pointing this bridge at the real board's IP once this PR is up is a
  five-minute check on their end, and only they touch the physical board
  per this repo's own hardware-access rule.

## Known v1 limitations (inherited from the firmware side, not this viewer)

- The board's framebuffer is not double-buffered, so a frame can show
  part-old, part-new pixels if something draws mid-send (documented in
  `explorer_screencast_demo.c`). This viewer renders whatever it receives;
  it doesn't try to hide tearing.
- One board-side viewer at a time. If someone else's TCP connection to
  port 4244 is already open, a new bridge connection will be refused by
  the board until that one closes.
- No frame-rate throttling on the wire — the board sends whenever its own
  dirty-rect flush fires, so displayed FPS reflects actual screen activity,
  not a fixed rate.
