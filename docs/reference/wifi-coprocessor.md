# WiFi co-processor: wiring

The physical link between the ITSboard and an ESP32-WROOM-32 DevKit acting
as a WiFi co-processor — one UART, two things that can run over it. This
page is wiring only. What's actually flashed onto the ESP32 today
(ESP32Marauder) and how to build it is
[marauder-coprocessor.md](marauder-coprocessor.md); the live-capture relay
built on top of that link is
[marauder-pcap-stream.md](marauder-pcap-stream.md); the deferred,
different-firmware internet-connectivity path this same header could
instead carry is `modules/wifi`'s own header comment.

## The connection

| ITSboard (CN8) | Signal | ESP32-WROOM-32 DevKit | Signal |
|---|---|---|---|
| Pin 8 (PC6) | USART6_TX | GPIO3 | RX0 |
| Pin 9 (PC7) | USART6_RX | GPIO1 | TX0 |
| Pin 1 or 12 | GND | GND | GND |

**Crossed, like any UART pair**: the board's TX goes to the ESP32's RX, and
the board's RX comes from the ESP32's TX — not pin-number-to-pin-number.

CN8 is a 12-pin single row on the ITSboard: `1 GND, 2 PE8, 3 PE10, 4 PE14,
5 PB10, 6 PB11, 7 PB0, 8 PC6, 9 PC7, 10 PC8, 11 PC9, 12 GND` (verified
against the board's own schematic, `docs/reference/datasheets/
ITSBRD-schematic-Jaehnichen-HAW-rev02.pdf`, sheet 5 "Analog and Timers" —
see `targets/itsboard/board.h`'s own `CADS_PIN_WIFI_*` block for the exact
citation). Pins 8/9 are silkscreened `TIM8_1`/`TIM8_2` on the board itself
— they are two of CN8's twelve timer-breakout pins, not a pair set aside
for this purpose; wiring USART6 here leaves TIM8_CH3/CH4 (PC8/PC9, CN8
pins 10/11) as CN8's only other still-free timer channels.

GPIO1/GPIO3 are the ESP32-WROOM-32's hardware UART0 pins — not a
software-configurable choice. Marauder's own firmware talks to its CLI
over the Arduino `Serial` object, which on this chip *is* UART0, so this
is the only pair that reaches it (see marauder-coprocessor.md's CLI
section on why the wiki's more general pinout guidance doesn't apply
here).

**Baud: 115200**, both sides. Marauder's firmware calls
`Serial.begin(115200)` in its own `.ino` — hardcoded upstream, not
negotiated — so `CADS_WIFI_BAUD` on the STM32 side must match it exactly.
(An earlier value of 460800 was left over from this same UART's deferred
PPPoS use, which wanted the higher rate for HDLC framing overhead; it
produced garbled bytes back from a real Marauder scan the first time this
link was tested end to end — the textbook symptom of a baud mismatch,
since a self-bridged TX-to-RX loopback test doesn't catch it: both ends
agree with *themselves*, just not with what's actually on the other end
of the wire.)

## Power: the ESP32 needs its own supply

GPIO1/GPIO3 (TX0/RX0) are the *same* pins the DevKit's onboard USB-serial
chip (CP2102 or CH340, depending on the clone) uses to talk to a host over
its own USB port. Powering the ESP32 from a Mac's USB cable while CN8 is
also driving those pins puts two transmitters on the same wire pair at
once — so during any session where the STM32 link is active, the ESP32
must be powered independently of that USB-serial chip (a separate 5V
supply, a USB cable that provides power but whose data lines go nowhere
useful, or any other source that isn't sharing the UART0 pins). The
DevKit's own USB port stays free for reflashing when the co-processor link
is idle; the two roles just aren't simultaneous.

A charge-only USB cable (power wires only, no D+/D-) is a different,
unrelated failure mode worth ruling out separately if the ESP32 seems
unresponsive during a Marauder rebuild/reflash session — see
marauder-coprocessor.md's own troubleshooting section for that symptom
(zero USB enumeration, not even a driverless device entry).

## On the bench

Wired via a breadboard: the ESP32-WROOM-32 DevKit seated on one side, CN8's
three relevant wires (TX, RX, GND) landing on it, and the DevKit's own
power pins fed from a separate supply tapped elsewhere on the ITSboard
rather than through its USB port — confirmed by webcam/bench photos during
this project's original wiring pass and rechecked against physical photos
again on 2026-08-28. A close macro shot is worth more than a wide one here
if a wire's exact destination is ever in question — CLAUDE.md's own
lessons-learned section has the story of a wire that looked misplaced near
the SWD header in a wide shot and turned out to be correctly seated on
CN8's own ground pin once examined up close.

## See also

- [ESP32Marauder co-processor](marauder-coprocessor.md) — building and
  flashing Marauder itself onto the wired ESP32.
- [Marauder PCAP-over-TZSP stream](marauder-pcap-stream.md) — the
  live-capture relay to Wireshark built on top of this link.
- `targets/itsboard/board.h`'s `CADS_WIFI_*` block — the pin/AF/baud
  definitions this page describes, with the shared-USART6/RS232 warning in
  full (don't use the onboard RS232 port while this link is wired).
