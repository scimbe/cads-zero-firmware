# Ethernet diagnostics: auto-negotiation inspector, link event log, and MAC counters

This covers `hal_eth_aneg.{c,h}`, `hal_eth_linklog.{c,h}` and
`hal_eth_mmc.{c,h}` — three Ethernet diagnostics built alongside
`hal_eth_mdio.c` (link/speed/duplex) and `hal_eth_tdr.c` (cable fault
distance). It does not document the rest of `targets/itsboard/hal/`, which
predates this convention.

`hal_eth_mmc` differs from the other two in kind, not just in what it reads:
it is not an MDIO/PHY feature at all, but memory-mapped MAC hardware. That
difference runs through every section below — read the "why" for `hal_eth_mmc`
separately rather than assuming it inherits the MDIO modules' reasoning.

## What is it?

`hal_eth_aneg` reads Registers 4 (ANAR, what this PHY advertises) and 5
(ANLPAR, what the link partner advertised back) and decodes both into
10/100 Mbit and half/full-duplex capability flags, pause support, and the
highest-common-denominator mode the two sides actually resolved to — the
part a raw register dump leaves the reader to work out by hand.

`hal_eth_linklog` polls Register 29 (Interrupt Source Flag) and turns its
latch-high/clear-on-read bits into a timestamped event log: link down, auto-
negotiation complete, remote fault.

`hal_eth_mmc` reads the STM32F429 MAC's built-in MMC (MAC Management
Counters) block: six free-running 32-bit counters — good frames transmitted,
good frames transmitted after one collision, good frames transmitted after
more than one collision, good unicast frames received, frames received with
a CRC error, and frames received with an alignment error — plus an
all-counters reset. There is no PHY involved and no MDIO transaction; these
are ordinary loads from `ETH->MMC*` registers inside the MAC itself.

## Why is it shaped this way?

**`hal_eth_aneg` and `hal_eth_linklog` are both read-only and non-disruptive**,
unlike `hal_eth_tdr_run()`. Auto-
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

**`hal_eth_mmc`'s six counters are the entire register set, not a curated
subset.** RM0090's MMC register map for this MAC has no broadcast,
multicast, oversize or undersize counters to leave out — the reserved-word
gaps in the CMSIS `ETH_TypeDef` struct between the registers that do exist
confirm it. Nothing here was scoped down for this project; it is scoped down
by the silicon.

**`cads_hal_eth_mmc_read()` returns `void`, breaking with the
early-return-false idiom the other MDIO-backed files in this directory use.**
An MDIO transaction can time out — no PHY at that address, a stuck bus — and
`false` reports that honestly. A load from `ETH->MMCTGFCR` cannot time out;
it is memory-mapped inside the MCU, not a serial transaction to an external
chip. Inventing a failure mode this hardware does not have would be worse
than not reporting one.

**MMCCR's Reset-On-Read bit is deliberately never set.** It would make
`cads_hal_eth_mmc_read()` zero every counter the instant it read them —
plausible-sounding for a "read one snapshot" API, except ROR is a MAC-wide
switch, not a per-caller one. Setting it would silently zero counters out
from under any other code that happens to read the same registers, which
this module has no way to know about and no business assuming does not
exist. `cads_hal_eth_mmc_reset()` (MMCCR.CR) is the only way this module
clears anything, and it is a call the caller has to make on purpose.

**No host unit test, unlike `hal_eth_aneg`/`hal_eth_linklog`.** Those two
are testable on the host because they never touch a register directly —
they call `cads_hal_eth_mdio_read()`, a function `tests/unit/fake_mdio.c`
can stand in for at link time. `hal_eth_mmc.c` has no such indirection to
substitute: `ETH->MMCTGFCR` is a load from a fixed address that exists only
on the target, the same as `hal_eth_mdio.c`'s own `ETH->MACMIIAR` accesses —
and `hal_eth_mdio.c`, the module that pattern is named after, has no host
test either. This follows that existing precedent (board code that touches
real registers is verified by the ARM build and on hardware, not by a host
fake) rather than inventing a register-injection seam this codebase does not
otherwise use.

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

`hal_eth_mmc` does not take a `phy` parameter and needs nothing initialised
of its own — only the MAC clock, which `cads_hal_eth_mdio_init()` already
enables as a side effect:

```c
#include "hal_eth_mdio.h"
#include "hal_eth_mmc.h"

void diagnostics_init(void) {
    cads_hal_eth_mdio_init(); /* also enables RCC_AHB1ENR_ETHMACEN */
}

void diagnostics_dump_counters(void) {
    cads_eth_mmc_counters_t counters;
    cads_hal_eth_mmc_read(&counters);
    /* counters.tx_good_frames, .rx_crc_errors, ... -> print, plot, whatever
     * the caller's display layer wants. */
}

void diagnostics_reset_counters(void) {
    cads_hal_eth_mmc_reset(); /* zeroes all six at once */
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
- **`hal_eth_mmc`'s counters wrap silently.** Hardware default is rollover,
  not saturate (MMCCR.CSR is left at its reset value of 0), so a 32-bit
  counter on a busy link can pass through zero between two reads. This
  module reports the raw value; a caller that wants a running total across a
  long session has to diff successive reads itself and account for wraparound
  the same way any free-running counter requires.
- **`hal_eth_mmc_reset()` clears all six counters together; there is no
  per-counter reset.** MMCCR.CR is a single bit for the whole block —
  wanting to zero just the CRC-error counter, say, is not something this
  register (or therefore this module) can do.
- **`hal_eth_mmc` does not enable the MAC clock itself.** It assumes
  `cads_hal_eth_mdio_init()` (or equivalent) already ran; reading before that
  reads through a peripheral whose clock is gated off, which on this MCU
  reads back as zero rather than faulting, and would misreport "no traffic"
  instead of "not initialised yet".
- **`hal_eth_mmc` has no host unit test.** See "Why is it shaped this way?"
  above — it is verified by the ARM build (zero new warnings under
  `cads_flags`) and, eventually, on hardware, the same as
  `hal_eth_mdio.c`/`hal_eth_tdr.c`, not by a host-side fake.
