# WiFi recon and live capture in Wireshark

From a bare ESP32-WROOM-32 DevKit to a live 802.11 capture streaming into
Wireshark on your Mac. About 30 minutes if you're wiring from scratch;
skip to §4 if the co-processor is already wired and flashed.

**Prerequisites:** an ITSboard already built and flashed (see
[Build and flash your first image](first-build.md)), an ESP32-WROOM-32
DevKit, a breadboard, jumper wires, and a USB cable for the ESP32 that
carries power *and* is separate from the Mac (see §1 for why). macOS or
Linux, `arduino-cli`, and Wireshark installed.

## 1. Wire the ESP32

Three wires, crossed, plus ground:

| ITSboard (CN8) | Signal | ESP32-WROOM-32 |
|---|---|---|
| Pin 8 (PC6) | USART6_TX | GPIO3 (RX0) |
| Pin 9 (PC7) | USART6_RX | GPIO1 (TX0) |
| Pin 1 or 12 | GND | GND |

Power the ESP32 from something other than the Mac — a separate 5V supply,
or a USB cable plugged into a charger rather than your computer. GPIO1/
GPIO3 are the same pins the DevKit's own USB-serial chip uses; sharing
them with a live USB host connection puts two transmitters on one wire at
once. Full reasoning and the CN8 pinout's schematic citation:
[WiFi co-processor wiring](../reference/wifi-coprocessor.md).

## 2. Build and flash ESP32Marauder

```bash
tools/marauder-build/build_and_flash.sh
```

This clones and patches the pinned ESP32Marauder source, builds it for the
`GENERIC_ESP32` target, and flashes it. If the upload hangs on
`Connecting...`, hold the DevKit's **BOOT** button, tap **EN**/reset once
while still holding BOOT, and keep holding until you see `Writing at
0x...`. Full recipe and the four upstream bugs this script patches around:
[ESP32Marauder co-processor](../reference/marauder-coprocessor.md).

Confirm it's alive - power the ESP32, then from the STM32 side:

```bash
scripts/board_cmd.py ~ 5
```

You should see the Marauder CLI echo back whatever it's given. Garbled
text instead of clean echoes almost always means a baud mismatch - see
that same reference page's troubleshooting section.

## 3. Point the relay at your Mac

Find your Mac's IP on the same network the ITSboard's Ethernet is on
(`ifconfig` / `ipconfig getifaddr en0`, or whichever interface applies),
then:

```bash
scripts/cads_config.py edit
```

Set, and save:

```ini
wifi.pcap_target = <your Mac's IP>
```

Push it if you didn't edit in place, then power-cycle the board or use
**Settings → Reload config** on the panel.

## 4. Start Wireshark listening

**Capture → Options**, select the **UDP Listener remote capture: udpdump**
interface. In its options: **Port** `37008` (should already be the
default), **Payload type** `TZSP`. Start the capture - Wireshark is now
listening, showing nothing yet.

## 5. Start the capture on the board

On the panel: **Marauder → Sniff (PCAP)**. The tool view shows a live
`Relayed: N` counter, climbing as it decodes frames off the ESP32's own
`sniffraw -serial` stream - a sign things are working even before you look
at Wireshark. Frames should start appearing in the Wireshark window within
moments of any nearby WiFi activity.

**Being honest about where this stands:** every piece of this pipeline is
individually verified — Marauder itself scans real networks
(marauder-coprocessor.md's own "Verified working" section), and the relay
code that turns its output into TZSP has full unit-test coverage
(`tests/unit/test_marauder_pcap.c`) — but the *whole chain*, ESP32 through
to a real Wireshark window, has not yet been run start to finish on real
hardware as of this writing. If you get here and it doesn't work, that's
useful to know — see
[the relay's own troubleshooting section](../reference/marauder-pcap-stream.md#troubleshooting)
for what to check first, in order.

## 6. Join a specific network (optional)

To have the ESP32 actually associate with a network rather than just
listen: set `wifi.enabled = 1`, `wifi.ssid`, and `wifi.password` the same
way as step 3, then **Settings → Join WiFi** on the panel. This scans for
the configured SSID and joins it by index - Marauder's own CLI has no
"join by name" command, only "join whichever scan result this is." See
`cads_marauder_join()`'s own doc comment
(`apps/marauder/cads_marauder.h`) if you want the full mechanism.

## Next

[WiFi co-processor wiring](../reference/wifi-coprocessor.md) and
[the PCAP relay's reference page](../reference/marauder-pcap-stream.md)
have the complete detail this tutorial only summarizes - reach for them
once something here needs debugging rather than following along.
