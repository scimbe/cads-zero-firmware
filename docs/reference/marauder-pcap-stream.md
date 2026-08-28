# Marauder PCAP-over-TZSP stream: live captures in Wireshark

The Marauder tool suite's **Sniff (PCAP)** entry relays every 802.11 frame
the ESP32Marauder co-processor captures (see
[marauder-coprocessor.md](marauder-coprocessor.md)) to a live Wireshark
window on your Mac over this board's own Ethernet link - no SD card, no
file transfer, no stopping the capture to look at it.

## How it actually gets there

Marauder's own `sniffraw -serial` command (see the pinned commit's
`Buffer.cpp::saveSerial()`) periodically writes raw pcap-format bytes onto
the *same* serial line its CLI text already shares, framed with the literal
ASCII markers `[BUF/BEGIN]` and `[BUF/CLOSE]`. `apps/marauder`'s own byte
parser (`cads_marauder_pcap.c`) demuxes that from ordinary CLI text on the
STM32 side, decodes each pcap record, wraps the raw 802.11 frame in a
minimal [TZSP](https://en.wikipedia.org/wiki/TZSP) header, and sends it as
one UDP datagram - over this board's real Ethernet/lwIP stack, not the
Marauder UART - to whatever IP `/config.txt`'s `wifi.pcap_target` names, on
port **37008**.

That port isn't an arbitrary choice: it's the exact default Wireshark's own
`udpdump` extcap tool listens on for TZSP, so pointing Wireshark at this
stream is "pick the payload type", not "guess a port number".

```
ESP32Marauder --(UART, CLI text + [BUF/..] framed pcap bytes)--> STM32
STM32 --(UDP, TZSP-encapsulated 802.11 frames, port 37008)--> udpdump --> Wireshark
```

## Setup

1. **Wire the target.** Add (or edit) this line in `/config.txt` (see
   [config-file.md](config-file.md)):

   ```ini
   wifi.pcap_target = 192.168.33.50   # your Mac's IP on the board's subnet
   ```

   Push it with `scripts/cads_config.py push` (or `edit`) and apply with
   **Settings → Reload config** - no reboot needed. The board and your Mac
   need a route to each other; the default static `192.168.33.0/24` (see
   `net.*` in config-file.md) already puts them on the same subnet if your
   Mac's interface is configured to match.

2. **Point Wireshark at the port.** In Wireshark: **Capture → Options**,
   select the **UDP Listener remote capture: udpdump** interface (or run
   `udpdump` directly via `tshark -i udpdump`). In its options, set:
   - **Port**: `37008` (the default - should already be filled in)
   - **Payload type**: `TZSP`

   Start the capture. Wireshark now has an active listener on that UDP
   port, decoding each datagram's TZSP header and showing the enclosed
   802.11 frame as if it came from a real Wi-Fi adapter in monitor mode.

3. **Start the capture on the board.** Marauder menu → **Sniff (PCAP)**.
   The tool view shows a running `Relayed: N` count (and the configured
   target, or a reminder to set `wifi.pcap_target` if it's still `0.0.0.0`)
   so you can confirm frames are actually being decoded even before
   Wireshark is listening. Frames should start appearing in the Wireshark
   window within moments of any nearby 802.11 traffic. **Stop Scan** (or
   leaving the tool view, which sends `stopscan` automatically - see
   `cads_marauder_tool_exit()`) ends the capture.

## What you'll see, and what you won't

- **Real 802.11 frames**, exactly as Marauder's own promiscuous-mode driver
  captured them - management frames (beacons, probe requests/responses,
  deauth/disassoc), not a summary or a re-encoding.
- **No original capture timestamp.** TZSP's tag list optionally carries one,
  but this relay doesn't populate it (see `cads_marauder_pcap_frame_cb_t`'s
  own comment) - Wireshark instead timestamps each frame by when its UDP
  datagram actually arrived, which on a live LAN link is close enough for
  watching traffic happen, not for precise inter-frame timing analysis.
- **Frames over 128 bytes are truncated, not dropped.** This firmware has no
  heap and a RAM margin measured in hundreds of bytes (see CLAUDE.md's own
  RAM-budget note), so the relay caps how much of any one frame it holds at
  once (`CADS_MARAUDER_PCAP_FRAME_MAX`,
  `apps/marauder/cads_marauder_pcap.h`). A truncated frame still arrives -
  Wireshark renders it with `[Frame is marked as truncated]`, the same as
  any pcap file captured with a short snap length - but its payload past
  that cap is gone. Deauth/disassoc frames (~26 B) and most management
  frames (a couple hundred bytes at most) fit whole; only unusually large
  data frames get cut.
- **No filtering on the board.** Everything Marauder's own `sniffraw`
  captures gets relayed; use Wireshark's own display filters
  (`wlan.fc.type_subtype == 0x0c` for deauth, for instance) to narrow what
  you're looking at.

## Troubleshooting

- **`Relayed: 0` and it never moves** - the STM32↔Marauder UART link itself
  isn't producing bursts. Confirm the ESP32 is actually running
  `sniffraw -serial` (the tool view sends this automatically on selection;
  a `~`-prefixed raw command from the Explorer console, see
  `docs/reference/explorer-console.md`, can confirm the raw UART is alive
  at all) and that nearby 802.11 traffic actually exists for it to capture.
- **`Relayed: N` climbing but nothing in Wireshark** - almost always
  `wifi.pcap_target` unset (still `0.0.0.0` - the tool view's own status
  line says so directly) or udpdump listening on the wrong port/payload
  type. Confirm the board and the Mac can reach each other at the IP layer
  (`ping` from the Mac to the board's `net.ip`, or vice versa via the
  Explorer's own ping command) before suspecting the relay itself.
- **Frames arrive but Wireshark shows garbage / fails to dissect** - check
  the payload type is set to `TZSP`, not raw 802.11 or Ethernet; udpdump
  needs to know how to interpret what's inside each datagram.

## Not yet hardware-verified

This feature was built and thoroughly host-tested (`tests/unit/
test_marauder_pcap.c` covers the marker/record demux, truncation,
byte-at-a-time resumability, and the TZSP header bytes exact-match against
Wireshark's own `packet-tzsp.c` constants) and passes the board's RAM
budget check, but has not yet been run end-to-end against real hardware and
a live Wireshark window. Treat this page as accurate about the *design*
until that verification happens - see `docs/ROADMAP.md`'s Log for the
current status.
