# Watch the board's screen live, in a browser

`tools/screencast-viewer` turns the board's screen-streaming TCP protocol
(`apps/bringup/explorer_screencast_demo.c`, port 4244) into a live,
remote-desktop-style view of the panel on the Mac, without a physical
camera pointed at the board. The stream itself is plaintext - see
[Board-Mac secure link](../reference/secure-link.md) if you need the
context on what is and isn't encrypted on this path today.

Pending merge as of this writing (PR #70) - once merged, this is checked out
with the rest of the repo, no extra step needed.

## Install

Nothing to install beyond Node.js itself (`>=18`, check with `node -v`) -
`tools/screencast-viewer` has zero npm dependencies.

```bash
cd tools/screencast-viewer
npm test        # optional: 14 unit tests against synthetic protocol data
```

## Use

1. Start the screencast listener on the board over the serial console (see
   [the explorer console reference](../reference/explorer-console.md)):

   ```bash
   scripts/board_cmd.py S 0      # 0 = run until a console byte arrives
   scripts/board_cmd.py S 300    # or a fixed 5-minute window
   ```

   The board only accepts one TCP viewer connection at a time - if a
   previous bridge process is still connected, stop it first.

2. Start the bridge, pointed at the board's IP:

   ```bash
   cd tools/screencast-viewer
   node bridge.js <board-ip> [http-port]   # default port 8642
   ```

   The board's IP depends on `net.dhcp` in its own config (see
   [Configure the firmware](configure.md)): with the repo's documented
   static default (`net.dhcp = 0`), that's `192.168.33.99`.

3. Open `http://localhost:8642` in a browser on the Mac. Multiple tabs can
   watch the same stream at once - the bridge fans the one board connection
   out to every connected browser.

The status line under the panel (`480 x 320 @ 4bpp`, fps, frame count)
confirms real frames are arriving, not just that the page loaded.

## What you're actually seeing

- Whatever `S <seconds>` itself draws - by default that's a fixed
  background colour plus a small moving marker
  (`cads_screencast_draw_marker()`), not the normal desktop/app UI. This is
  a wire-protocol demo, not a way to remote-control the board's own GUI -
  for that, see
  [Driving the GUI headlessly](../reference/explorer-console.md#driving-the-gui-headlessly).
- No double-buffering on the board side: a frame can show part-old,
  part-new pixels if something draws mid-send. The viewer renders whatever
  it receives and does not try to hide tearing.
- No fixed frame rate - the board sends whenever its own dirty-rect flush
  fires, so displayed fps reflects actual screen activity.

## Verification status

Hardware-verified 2026-08-29: real board at `192.168.33.99:4244`, real
frames (480x320, 4bpp, correct palette) rendered live in an actual browser
tab, confirmed via screenshot - not just the bridge's own synthetic-data
unit tests. See `tools/screencast-viewer/README.md` for the full protocol
writeup and the pre-hardware verification (unit tests + a mock-board smoke
test) that came from the PR itself.
