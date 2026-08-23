# `apps/netinfo` — link state, IP address and the PA7 blackout notice

## What is it?

A single read-only view (`CADS_VIEW_ID_NETINFO`, `0x0600`) that shows Ethernet
link and IP status. `cads_netinfo_init(dispatcher)` registers it; `apps/menu`
calls that for you, behind its own `CADS_APP_NETINFO_ENABLED` guard, alongside
the other optional apps. The view has no input handler, so an unconsumed Back
falls through to the dispatcher's default pop. On `enter()` it takes one
snapshot — board capability from `cads_hal_board_info()->has_network` and
live link state from `cads_net_status()` (`modules/net`) — into its own
`cads_netinfo_status_t`, then `draw()` paints seven label/value rows
(Interface, Status, Speed, IP address, Lease, Gateway, DNS server) followed by
three static lines pointing at the PA7 pin conflict and the doc that explains
it.

## Why is it shaped this way?

**`has_network` and the rest of the status come from two different sources
because they answer two different questions.** `has_network` is a board
capability — does this hardware have an Ethernet MAC at all — read once from
`cads_hal_board_info()`. Everything else (`link_up`, `speed_mbit`,
`full_duplex`, the addresses, `dhcp_bound`) is a runtime fact about whether a
cable is plugged in and DHCP has bound, read from `cads_net_status()`. A
board with no Ethernet MAC should never even ask that second question, and
one that has a MAC but no cable connected is a completely different state
from "no MAC exists" — collapsing the two into one bool would lose that
distinction.

**This view used to have nothing real to read, and the code remembers it.**
The in-file comment above the status struct notes that `cads/net/net.h` is
"the portable service this file's own comment used to say did not exist
yet." Per the project history: netinfo was originally built with
`link_up`/`speed_mbit`/`ip_address` as fixed placeholders, because only
MDIO-based PHY management existed — there was no data path to read a real
link state from without a portable app reaching into a board-only header,
which `docs/reference/module-layout.md` forbids. `cads/toolbox/record.h`
(a named registry) exists specifically because of that anticipated gap —
its own header comment cites this exact app wanting the PHY's link state
from a board-only driver as the motivating case. Once `modules/net` shipped
its own board/simulator split (the same pattern `cads/storage/flash.h`
uses), that plan was overtaken: netinfo now links `cads_net` directly and
calls `cads_net_status()`, no registry indirection needed.

**The speed-text buffer is sized for the type's worst case, not the
realistic one, because the realistic sizing already broke once.** `speed_mbit`
is a `uint16_t`, so "65535 Mbit, half duplex" is the longest string that
formatting it could ever produce; `speed_text[32]` is sized for that string,
not for the 10/100/1000 values any real PHY reports. An earlier version was
sized for the realistic case and tripped a genuine `-Wformat-truncation`
warning that had to be fixed later — the current size exists so the compiler
has nothing left to warn about.

**The "Interface:" text is a hardcoded string, not a driver query, because
there is exactly one hardware configuration to describe.** `docs/HARDWARE.md`
§5 states this board's Ethernet as LAN8742A over RMII; that string is baked
in behind the `has_network` bool rather than composed from anything
`cads_net_status()` reports, because nothing in this codebase currently
builds for a board with a different PHY.

**The three footer lines cite `docs/explanation/pa7-conflict.md` because
that document, not this file, owns the number.** The display and the
Ethernet MAC time-share PA7 — `SPI1_MOSI` and `ETH_RMII_CRS_DV` have no
alternate pin mapping on this MCU — so every panel redraw blacks out the
receiver for one SPI-band's worth of time. 22.5 ms is that document's
measured figure for the `/16` divider, which is also the "safe" default
`apps/settings` seeds the hardware with at boot. The two apps agree because
they cite the same fact, not because this view reads settings' state (it
doesn't — see limits).

**Refresh happens on `enter()`, not on `draw()`, because that is the
contract `cads_view.h` documents.** A view's `draw()` callback is invoked
only when the compositor has already decided the view is dirty; reloading
data belongs in `enter()`, which the compositor calls once per navigation
into the view and marks fully dirty around. Netinfo follows that literally:
`cads_netinfo_refresh()` is wired to `enter()` only, so a snapshot is taken
once per visit, not once per frame.

## How do I use it?

```c
cads_netinfo_init(&dispatcher); /* called by apps/menu; not usually direct */
```

## What are the limits?

- **It is a snapshot, not a live view.** `cads_netinfo_refresh()` runs from
  `enter()` alone; `draw()` never calls it and there is no polling or timer.
  If the link comes up, DHCP binds, or the cable is unplugged while this
  screen is already open, nothing on screen changes until the user leaves
  the view and re-enters it.
- **It shows less than `cads_net_status()` actually returns.** That struct
  also carries `mac[6]`, `rx_frames`, `tx_frames` and `rx_dropped`; none of
  them are copied into `cads_netinfo_status_t` or drawn, even though this
  file already includes `cads/toolbox/fmt.h`, whose `cads_fmt_mac()` exists
  for exactly the formatting job a MAC row would need.
- **The PA7 footer text is static, not derived from the current SPI
  divider.** `apps/settings` can switch the display clock from `/16` to `/8`,
  which per `docs/explanation/pa7-conflict.md` halves the worst-case blackout
  to 11.5 ms — but this view always prints the `/16` figure regardless of
  which clock is actually active, and has no way to read that state even if
  it wanted to (per `apps/settings`' own README, the HAL exposes no getter
  for it).
- **No input handling beyond Back.** The view cannot be scrolled or
  manually refreshed; its layout is a fixed seven rows plus three lines at a
  constant `CADS_NETINFO_ROW_HEIGHT`/`CADS_NETINFO_LABEL_WIDTH`, not a
  reflowable or scrollable list.
- **`has_network` is a board fact, not a per-boot cable check.** A board
  built with `has_network == false` always shows "not present on this
  board," regardless of what `cads_net_status()` would say — there is no
  path in this app for a board without Ethernet hardware to still probe for
  one.
