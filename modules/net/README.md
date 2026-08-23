# `modules/net` — lwIP integration, RMII MAC glue, the network status API

## What is it?

CaDS Zero's own integration layer over vendored lwIP (`lib/lwip`): the glue
that turns `hal_eth_mac.h`'s raw MAC/DMA driver into a lwIP `struct netif`,
and the small portable API declared in `include/cads/net/net.h` -
`cads_net_init()`, `cads_net_poll()`, `cads_net_status()`, plus three
blocking diagnostic probes (`cads_net_arp_probe()`, `cads_net_ping()`,
`cads_net_traceroute_probe()`) - that everything above it calls instead of
touching lwIP directly. Two implementations sit behind that one header, the
same split `cads/storage/flash.h` uses: `src/cads_net_board.c` on the
itsboard target wires lwIP's raw `NO_SYS` API to the real RMII MAC and PHY,
and `src/cads_net_sim.c` on the host reports "no link, ever" - there is no
RMII hardware in the simulator and no plan to fake one. This is the module
every M5 explorer network command builds on (`N R B U O W F A P T I G C M H
S`), which makes it the largest and most heavily used module in the project.

## Why is it shaped this way?

**One poll loop, no OS, no sockets.** `lwipopts.h` sets `NO_SYS=1`,
`LWIP_NETCONN=0`, `LWIP_SOCKET=0`, and `SYS_LIGHTWEIGHT_PROT=0` - this
firmware runs one bare-metal main loop and polls lwIP from it exactly the
way it polls the display and input drivers (`apps/bringup/tasks.c`). There
is only ever one caller of the internal state, so lwIP's own locking would
be pure overhead.

**Link state is watched, not assumed.** `cads_net_link_check()` re-reads
`cads_hal_eth_phy_status()` on every `cads_net_poll()` call, and only then
calls `cads_hal_eth_mac_init()`/`_start()` and
`cads_hal_spi_set_eth_datapath_active(true)`. The PHY may not have a cable
plugged in when `cads_net_init()` runs, and the MAC/DMA must not be started -
nor PA7 claimed from the display - until there is an actual link to receive
on (`hal_eth_mac.h`'s own contract, `docs/explanation/pa7-conflict.md`). On
the down transition it calls `dhcp_stop()`, not `dhcp_release_and_stop()`:
the link is already gone by the time that runs, so there is no carrier left
to send a DHCPRELEASE over.

**`cads_net_init()` is idempotent on purpose.** Two independent callers in
this firmware want networking "on" without knowing about each other -
`explorer_eth.c`'s `h` command and the app tree
(`explorer_app_demo.c`) both call it, and in practice both pass the same
locally-administered MAC (`02:CA:D5:5E:00:01`, from
`cads_explorer_net_mac()`) - but the design does not lean on that agreement:
a static `initialised` flag makes every call after the first a no-op, and
`mac_address` on those later calls is ignored outright, because calling
`netif_add()` twice on the same static `struct netif` would corrupt lwIP's
own netif list.

**No malloc-backed RNG.** `arch/cc.h` cannot use newlib-nano's `rand()`: it
lazily `malloc()`s a state table on first call, which pulls in `_sbrk`, and
this firmware's linker script defines no heap, ever - the same failure class
that hit littlefs's default assert in `modules/storage`. `cads_net_board.c`
seeds and drives its own xorshift32 (`cads_lwip_rand_state`, seeded from
`cads_hal_ticks_ms() | 1u` so it can never lock at the all-zero fixed point)
and wires it in as `LWIP_RAND()`. `LWIP_PLATFORM_ASSERT` is routed to
`cads_hal_panic()` for the identical no-heap reason, not through libc's
`abort()`.

**RAM is hand-tuned against the linker's headroom guard.** `MEM_SIZE` is
2048 bytes (down from 4096, then 3072), `PBUF_POOL_SIZE` is 4 (down from 8),
and `MEMP_NUM_RAW_PCB` is 1 - `lwipopts.h` documents each cut against
`targets/itsboard/linker/cads_itsboard.ld`'s `ASSERT(__cads_heap_size >=
48K, ...)`. The raw-pcb limit is not arbitrary: `cads_net_arp_probe()`,
`cads_net_ping()` and `cads_net_traceroute_probe()` each create exactly one
`raw_pcb` and remove it before returning, so more than one was never
needed. `DNS_TABLE_SIZE`/`DNS_MAX_SERVERS` are trimmed to 1 for the same
reason: this module only ever reads the DHCP-supplied DNS server address for
display, never resolves a hostname, and lwIP's default `dns_table_entry`
carries a 256-byte hostname buffer per slot that this firmware would be
paying for and never using.

**Probes block the caller.** `cads_net_arp_probe()`, `cads_net_ping()` and
`cads_net_traceroute_probe()` each call `cads_net_poll()` and
`cads_hal_delay_ms()` in their own loop up to `timeout_ms`, so no caller
needs its own wait loop - but on this single bare-metal loop that also means
the whole firmware is unresponsive for up to `timeout_ms` if nothing
answers. All three return their "nothing sent" result immediately, without
touching the network, when the link is not up - there is nothing to probe a
subnet through yet.

**Traceroute trusts message type and arrival order, not payload
validation.** `cads_net_traceroute_recv()` accepts an `ICMP_TE` (time
exceeded) reply without checking that its embedded original-packet payload
(RFC 792: IP header plus 8 bytes) actually echoes this probe's id/seqno.
One probe is in flight at a time with a short timeout, which is enough for a
diagnostic tool on a LAN - not the adversarial-network-resistant validation
a routing device's own ICMP handling would need.

**Frames are flattened before they reach the driver.** lwIP's pbufs may be
chained, but `cads_hal_eth_mac_transmit()` wants one contiguous buffer (its
own "copies into the next free TX buffer" contract), so
`cads_netif_linkoutput()` copies into a static 1536-byte staging buffer
(`CADS_NET_TX_STAGING_SIZE`) sized to match the driver's own per-descriptor
buffer, not derived from the 1500-byte MTU directly.

## How do I use it?

```c
#include "cads/net/net.h"
#include "cads_hal.h"

/* Locally-administered address (bit 1 of the first octet set) - the caller
 * owns the choice; this is the scheme apps/bringup uses. */
static const uint8_t mac[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x01};

void net_demo(void) {
    cads_net_init(mac); /* idempotent - safe even if another caller already did this */

    /* Bring the netif up and give DHCP a few seconds to bind. */
    uint32_t deadline = cads_hal_ticks_ms() + 5000u;
    cads_net_status_t status;
    do {
        cads_net_poll(); /* call every loop iteration - cheap when idle */
        cads_net_status(&status);
    } while(!status.dhcp_bound && (int32_t)(cads_hal_ticks_ms() - deadline) < 0);

    if(!status.link_up) return; /* nothing to probe through yet */

    uint32_t rtt_ms = 0u;
    if(cads_net_ping(status.gw_addr, 1000u, &rtt_ms)) {
        /* gateway answered within 1 s; rtt_ms holds the round trip */
    }
}
```

Link with `target_link_libraries(<target> PRIVATE cads_net)` and include as
`cads/net/net.h`. On the itsboard target this pulls in vendored lwIP
(`cads_lwip`) automatically; on the simulator it does not.

## What are the limits?

- **The simulator never has link, by design.** `cads_net_sim.c` reports
  `link_up = false` unconditionally and every probe returns false / rejects
  immediately - any app that needs to test real network behaviour has to do
  it on the board (`docs/ROADMAP.md`'s hardware-gate discipline), not by
  fooling this module into pretending on host.
- **One netif, wired once, for the whole firmware image.** `cads_net_init()`
  cannot be used to add a second interface or to reconfigure the existing
  one after the first call; there is no teardown.
- **Raw `NO_SYS` API only** - `LWIP_NETCONN=0`, `LWIP_SOCKET=0`. Nothing
  above this module gets BSD sockets or lwIP's netconn API.
- **Only one blocking probe in flight at a time.** `MEMP_NUM_RAW_PCB=1`
  backs `cads_net_arp_probe()`/`cads_net_ping()`/`cads_net_traceroute_probe()`;
  each is fully synchronous and busy-waits up to `timeout_ms`, so nothing
  else network-related can be started concurrently and the bare-metal loop
  is stalled for the duration.
- **No IPv6** (`LWIP_IPV6=0`).
- **No hostname resolution.** `LWIP_DNS` is on only far enough to read the
  DHCP-supplied server address (`DNS_TABLE_SIZE`/`DNS_MAX_SERVERS=1`); this
  module never looks up a name.
- **TX frames larger than 1536 bytes are rejected** (`ERR_BUF`) before they
  reach the MAC - a fixed staging-buffer limit, not a general jumbo-frame
  path.
- **No hardware checksum offload.** `CHECKSUM_GEN_*`/`CHECKSUM_CHECK_*` are
  all software; every packet pays a software checksum pass.
- **Frames lost during a display blit are invisible to this module**, not
  just uncounted: while `cads_hal_spi_claim_bus()` holds PA7 the MAC's
  receiver is stopped at the hardware level, and `rx_dropped` in
  `cads_net_status_t` only counts frames lost above that point (the RX pbuf
  pool exhausted) - see `docs/explanation/pa7-conflict.md` for the blackout
  bound (22.5 ms at the default SPI clock, 11.5 ms at the fast one).
- **Traceroute hop identification is a heuristic**, not RFC-792-payload
  verified - adequate for a LAN diagnostic tool, not for an adversarial
  network.
- **Nothing here is thread-safe or interrupt-safe.** Every function assumes
  the single bare-metal poll-loop calling convention this firmware uses
  throughout; none of it may be called from an ISR or a second thread.
