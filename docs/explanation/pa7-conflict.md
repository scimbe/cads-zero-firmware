# The PA7 conflict

If you remember one thing about this hardware, make it this one.

## The problem

**`SPI1_MOSI` and `ETH_RMII_CRS_DV` are the same physical pin.**

The Waveshare shield's display data line arrives on Arduino header pin D11. On a
NUCLEO-F429ZI, D11 is bonded to **PA7** by the board's default strapping.

`ETH_RMII_CRS_DV` — carrier sense / data valid, which the MAC needs on every
received frame — has exactly **one** possible location on the STM32F429: PA7.
There is no alternate mapping to move it to. Checking the datasheet's alternate
function table for a second option is the first thing everyone tries, and there
isn't one.

A pin has one alternate function at a time. So the display and the Ethernet MAC
cannot both be connected. Whichever driver initialises last wins the mux.

## How this shows up in practice

It is not a theoretical concern. In the ITS lab's existing `Stack` project the
sequence is:

```c
initITSboard();
GUI_init(DEFAULT_BRIGHTNESS);   // claims PA7 for SPI1, AF5
lcdPrintlnS("LWIP-project");    // works
init_lwip_stack();
netif_config();                 // HAL_ETH_MspInit claims PA7 for ETH, AF11
                                // from here on the display is mute
```

The greeting prints before the network comes up, so nothing looks wrong. Every
display write after `netif_config()` goes nowhere.

Their driver does handle it, in `LCD_SPI.c`:

```c
uint8_t SPI4W_Write_Byte(uint8_t value){
    uint32_t ethConfig = ETH->MACCR & (ETH_MACCR_TE | ETH_MACCR_RE);
    ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);       // stop the MAC
    while (ETH->DMASR & ETH_DMASR_TS) {}
    while (ETH->DMASR & ETH_DMASR_RS) {}
    save_restore_AltFn_SPI1_MOSI_Pin();                 // steal the pin
    /* ... send ONE byte ... */
    save_restore_AltFn_SPI1_MOSI_Pin();                 // give it back
    ETH->MACCR |= ethConfig;                            // restart the MAC
}
```

This is correct. It is also brutal: the MAC is torn down and restarted **per
byte**. A full-screen redraw is 307 200 bytes, so that is 307 200 stop/start
cycles for one frame. It also makes DMA structurally impossible — you cannot
flip an alternate function in the middle of a DMA burst — which caps the display
at polled byte-at-a-time speed.

## What this firmware does instead

Same arbitration, moved up a level. `cads_hal_spi_claim_bus()` and
`cads_hal_spi_release_bus()` bracket an **entire blit**:

```
claim:    stop MAC → drain in-flight frames → steal PA7
          set window, issue RAMWR
          DMA the whole rectangle
release:  wait for SPI to go idle → return PA7 → restart MAC
```

One stop and one start per rectangle instead of per byte. Three orders of
magnitude fewer MAC restarts, and DMA becomes usable, which is where the actual
throughput comes from.

The claims nest, so a driver that needs several operations under one lock — the
touch controller does — takes the bus once around all of them.

It is still a compromise. Frames arriving during a blit are lost, and a
full-screen redraw takes 448 ms, which is a long time to have the receiver off.
For a UI that redraws small dirty rectangles this is tolerable; for sustained
throughput it is not.

## The actual fix

UM1974, the Nucleo-144 user manual, section 6.9:

> **SB121, SB122 (D11)** — ON, OFF: D11 (Pin 14 of CN7) is connected to STM32
> **PA7** (SPI_A_MOSI/TIM_E_PWM1). OFF, ON: D11 (Pin 14 of CN7) is connected to
> STM32 **PB5** (SPI_A_MOSI/TIM_D_PWM2).

ST anticipated this exact collision and left an escape hatch. Swapping the two
solder bridges moves the display's data line to **PB5**, which is otherwise
unused on this board, and leaves PA7 to the PHY alone.

Afterwards:

```bash
scripts/build.sh Debug -DCADS_SPI_MOSI_ON_PB5=1
```

and all the arbitration compiles away — `cads_hal_spi_claim_bus()` becomes
empty. Display and Ethernet then run concurrently at full speed, and the display
can be pushed to a faster SPI divider without stealing time from the network.

### The decision: no modification

**Decided 2026-08-18: the board stays as it is.** No soldering.

The reasoning is straightforward once the alternatives are laid out. The swap
is reversible in principle, but it is physical work on a lab board that other
people also use, it makes this firmware's requirements diverge from every other
project on the same hardware, and a board modified for one project's
convenience is a board that surprises the next person who picks it up. The
firmware keeps `CADS_SPI_MOSI_ON_PB5=0` permanently.

That is not a compromise this project merely tolerates — it is a constraint it
now designs around, and the rest of this page is what follows from it.

## Living with the time slice

### What it actually costs

While the display owns PA7, the MAC's receiver is off and arriving frames are
lost. So the number that matters is not the total redraw time but the **longest
uninterrupted blackout**.

The flush path already helps here, and by accident of a decision made for
another reason: `cads_canvas_flush()` converts and pushes the damaged region in
**bands** of at most sixteen rows, and `cads_hal_display_blit()` claims and
releases the bus per call. The MAC therefore comes back up between bands rather
than staying down for the whole frame.

| | at /16 | at /8 |
|---|---|---|
| One 480×16 band | **22.5 ms** | 11.5 ms |
| Full screen (20 bands) | 448 ms total | 229 ms total |
| **Longest single blackout** | **22.5 ms** | **11.5 ms** |

22.5 ms is a long time on a 100 Mbit link — about 280 KB of wire time — but it
is twenty times better than the 448 ms a naive single-transfer implementation
would produce, and it is bounded rather than proportional to the redraw size.

### What this means for the design

**Dirty rectangles stop being an optimisation and become a network feature.** A
40×40 update is one band: a single 4.7 ms blackout. A full-screen redraw is
twenty of them back to back, during which the receiver is down roughly 95 % of
the time. The rule for any app that also uses the network is therefore the same
rule the display already imposed for its own reasons — redraw what changed, not
the screen.

**TCP absorbs this; UDP does not.** A stalled receiver during a redraw looks to
TCP like a brief burst of loss, which it recovers from with a retransmit and a
window adjustment. Datagram traffic simply loses whatever arrived during the
window. Anything built here that cares about individual datagrams — a discovery
protocol, a telemetry stream — has to tolerate that or repeat itself.

**The faster SPI divider is now a network decision too.** Moving from /16 to /8
halves every blackout. It is qualified on hardware already (669 kpixel/s
measured) and only awaits visual confirmation that the shift register chain
latches cleanly at 11.25 MHz.

**Screen streaming over the network is self-limiting**, which is worth knowing
before someone designs it: pushing the framebuffer to a host requires the
display *not* to be redrawing, and redrawing is what makes the framebuffer
worth pushing. It works, but the two compete for the same pin.

### What is deliberately not done

Splitting bands smaller than sixteen rows to shorten the blackout further was
considered and rejected for now. Each band costs a bus claim, a MAC stop and
restart, and a window-setting command sequence; halving the band height doubles
that overhead to halve a blackout that TCP already handles. If a real workload
turns up that suffers, measure it first — the number to beat is 22.5 ms, and it
is in `docs/reference/measurements.md`.

## Consequences for anyone writing code here

- **Never touch the display or the touch controller outside a claim/release
  pair.** Doing so corrupts whatever the PHY is receiving at that moment, and
  the symptom (occasional dropped frames under load) is miserable to track down.
- **Never reconfigure an RMII pin** outside the Ethernet driver: PA1, PA2, PC1,
  PA7, PC4, PC5, PG2, PG11, PG13, PB13.
- Assume the pin can be taken from you. Code that caches "the SPI is configured
  for the display" across a yield is wrong.
