# Ethernet diagnostics: auto-negotiation inspector and link event log

This covers `hal_eth_aneg.{c,h}` and `hal_eth_linklog.{c,h}` — the two
MDIO-only diagnostics built alongside `hal_eth_mdio.c` (link/speed/duplex)
and `hal_eth_tdr.c` (cable fault distance). It does not document the rest of
`targets/itsboard/hal/`, which predates this convention.

## What is it?

`hal_eth_aneg` reads Registers 4 (ANAR, what this PHY advertises) and 5
(ANLPAR, what the link partner advertised back) and decodes both into
10/100 Mbit and half/full-duplex capability flags, pause support, and the
highest-common-denominator mode the two sides actually resolved to — the
part a raw register dump leaves the reader to work out by hand.

`hal_eth_linklog` polls Register 29 (Interrupt Source Flag) and turns its
latch-high/clear-on-read bits into a timestamped event log: link down, auto-
negotiation complete, remote fault.

## Why is it shaped this way?

**Both are read-only and non-disruptive**, unlike `hal_eth_tdr_run()`. Auto-
negotiation advertisement and interrupt flags can be inspected at any time,
including while a link is actively carrying traffic, with zero risk of
knocking it down — that is what makes them safe to call from, say, a status
screen that refreshes every second.

**The log is polled, not interrupt-driven.** Register 29's bits are
documented as latch-high, clear-on-read, which is precisely the contract a
poll loop wants: nothing is missed between polls, because the latch survives
until read. Wiring the PHY's nINT pin to an EXTI line would need one more
GPIO, one more vector table entry, and — because MDIO transactions are not
interrupt-safe on this MAC — a deferred-work queue to move the actual
register read out of the ISR. Polling gets the same events for a fraction of
that, at the cost of collapsing two same-kind events between polls into one
log entry and timestamping "when this code noticed" rather than "when the
PHY latched it". For a diagnostics log rather than a real-time protocol
state machine, that trade is the right one.

**The resolved mode exists because the individual bits do not answer the
question a mismatch diagnosis actually asks.** `hal_eth_mdio.c`'s
`speed_mbit`/`full_duplex` already report what this PHY resolved to via the
vendor SCSR register; `cads_eth_aneg_report_t.resolved` recomputes the same
answer independently, from the two standard advertisement registers by IEEE
802.3 Table 28B-3 priority — so a caller that sees the two disagree has
found a real anomaly, not narrowed down to one already-correct number.

**The log is a fixed-size ring in the caller's struct, not a hidden
singleton.** No `malloc`, and no static instance that would silently share
state between two callers or make the module untestable without also
resetting some global — see `docs/reference/module-layout.md`.

## How do I use it?

```c
#include "hal_eth_aneg.h"
#include "hal_eth_linklog.h"

static cads_eth_linklog_t link_log;

void diagnostics_init(void) {
    cads_eth_linklog_init(&link_log);
}

void diagnostics_tick(uint8_t phy) {
    /* Cheap enough to call from a 1 Hz status-screen refresh. */
    (void)cads_hal_eth_linklog_poll(phy, &link_log);

    cads_eth_aneg_report_t report;
    if(cads_hal_eth_aneg_report(phy, &report) && report.completed) {
        if(report.resolved == CadsEthAnegModeNone) {
            /* Nothing in common - the link should not be up at all. */
        } else if(report.local.full_100 && report.resolved != CadsEthAnegMode100Full) {
            /* This PHY offered 100 Mb full duplex; the partner did not take
             * it. Worth putting on a screen, because "the link works but is
             * slower than it should be" is exactly what this catches. */
        }
    }
}

void diagnostics_dump(uint8_t phy) {
    (void)phy;
    for(uint32_t i = 0; i < cads_eth_linklog_count(&link_log); i++) {
        const cads_eth_link_event_t* event = cads_eth_linklog_at(&link_log, i);
        /* event->timestamp_ms, event->type -> print, plot, whatever the
         * caller's display layer wants. */
    }
}
```

## What are the limits?

- **`hal_eth_aneg` reports the registers as found; it does not wait for
  negotiation to finish.** Call it while `autoneg_done` (from
  `hal_eth_mdio.c`'s `cads_eth_phy_status_t`) is false and `partner` simply
  reflects a stale or zero ANLPAR — that is not a bug in this module, it is
  the caller asking too early.
- **`hal_eth_linklog` cannot separate two same-kind events that both latch
  between two polls** — Register 29 only reports "this happened at least
  once since the last read", not a count. Poll faster if that resolution
  matters; the log's `dropped` counter tracks entries lost to a full ring,
  not events collapsed by the register itself, and there is no way to tell
  the two apart from software.
- **100BASE-T4 is not decoded.** The LAN8742A never advertises it, so
  `cads_eth_aneg_resolve()` omits it from the priority order entirely rather
  than carry a code path that can never execute on this PHY.
- **Neither module owns the PHY address or the MDIO bus.** Both take `phy`
  as a parameter and call straight into `hal_eth_mdio.c`; they do not probe,
  cache, or validate it themselves.
