# The bring-up explorer console

`apps/bringup` builds a second firmware entry point, separate from the real
desktop/menu app tree: a single-letter command console over the same USART
the ST-Link exposes as a virtual COM port (`/dev/cu.usbmodemXXXX` at
`115200 8N1`). It exists because most of this board's own subsystems (the
Ethernet MAC/PHY, the adapter's GPIO banks, the display bus) have no way to
report their own state to a human without a driver already being trusted —
this console is what let each one get bootstrapped and hardware-gated before
the real GUI app that eventually wraps it existed.

Every command is a single character, optionally followed by one or two
whitespace-separated arguments; unrecognised input restates the same one-line
help this page documents. Send `?` at any time to get it fresh from the
firmware — this page is a reference, the firmware's own help string is the
ground truth if the two ever disagree.

`scripts/board_cmd.py <letter> [arg] --timeout N` runs one command
non-interactively from a host shell; `scripts/board_test.py` drives the boot
self-test and, with `--suite m6`, a handful of these commands as a scripted
pass/fail check. See [Run the hardware gate](../how-to/board-test.md).

## Bring-up and diagnostics

| Cmd | Args | Does |
|---|---|---|
| `?` | — | Reprint this help text |
| `i` | — | Dump the input data register (IDR) of every port, once |
| `w` | `<sec>` (20) | Watch all ports for changes — the fastest way to find which pin a button you're pressing is wired to |
| `k` | — | Task stacks, task count, input counters |
| `t` | — | One touch sample from the XPT2046 |
| `s` | `<sec>` | Live button state S0..S7 and touch, streamed |
| `q` | `<n>` (200) | Touch soak: sample the touch controller `n` times untouched and count ghost touches |
| `x` | — | Kernel test: `cads_timer` + `cads_event` exercised under the FreeRTOS scheduler |
| `r` | — | Toolbox self-test: `cads_pubsub` + `cads_record` |

## Display

| Cmd | Args | Does |
|---|---|---|
| `p` | `<n>` | Draw a test pattern: `0` black, `1` blue, `2` green, `3` quadrants, `4` stripes, `5` splash, `6` fonts |
| `f` | `<0\|1>` | Display SPI clock: `0` = `/16` (safe, 342 kpixel/s, 22.5 ms Ethernet blackout per redraw band), `1` = `/8` (fast, 669 kpixel/s, 11.5 ms blackout) — see [the PA7 conflict](../explanation/pa7-conflict.md) |
| `b` | `<pct>` | Backlight, 0-100% |
| `l` | `<rgb>` | On-board LEDs, e.g. `l 100` |
| `g` | `<sec>` (20) | GUI smoke test: `apps/gpio` live on the panel |
| `d` | `<sec>` (30) | App tree live: desktop → menu → app, exercising the real GUI/view-dispatcher stack rather than a standalone demo |

## Storage

| Cmd | Args | Does |
|---|---|---|
| `u` | — | M4 hardware gate: format + write on first run, or verify the same data survived a reset — see `docs/SAFETY.md` for the flash-write window this respects |
| `y` | — | Raw flash driver diagnostic, bypassing littlefs entirely — the tool for debugging a failing `u` |
| `v` | `<sec>` (30) | File browser live on the panel |

## Ethernet PHY (LAN8742A, direct register / MDIO)

These talk to the PHY below lwIP — they work whether or not a netif is up,
and are the first thing to reach for when link behaviour looks wrong before
suspecting the stack.

| Cmd | Args | Does |
|---|---|---|
| `e` | — | PHY identity and link state, over MDIO |
| `a` | — | Auto-negotiation inspector, over MDIO |
| `n` | — | Link event log: poll and dump, over MDIO |
| `m` | — | MAC traffic counters, read directly from the MMC registers — no MDIO, works even if the PHY is unresponsive |
| `c` | — | Cable test: TDR + matched-pair length estimate, over MDIO |

!!! warning "`c` is disruptive"
    The cable test forces the PHY through its own diagnostic mode, which
    drops the link for its duration. Expect the netif (and anything using
    it) to see a link-down/link-up cycle.

## Network (lwIP, M5)

Everything below brings up (or assumes) a live lwIP netif via
`cads_net_init()`/`cads_net_poll()` and runs for a bounded duration before
printing a summary and returning control to the console. This bench has no
DHCP server on its segment, so link-local/no-lease behaviour in several of
these is the expected, not the failing, case — see `docs/ROADMAP.md`'s M5
log entries for what was actually observed here.

| Cmd | Args | Does |
|---|---|---|
| `h` | `<sec>` (20) | M5 net gate: bring up the netif, poll, report packet/byte counters |
| `j` | `<sec>` (30) | `cads_cli` live, both on this same serial console and over TCP `:4242` |
| `S` | `<sec>` (30) | Screen streaming: framebuffer + a moving marker over TCP `:4244` |
| `H` | `<sec>` (30) | HTTP status page over TCP `:80` |
| `A` | `<hex-base> [count]` (32) | ARP scan of a subnet, e.g. `A c0a80100 20` scans `192.168.1.0/24` for 20 addresses |
| `P` | `<hex-target> [count]` (4) | Ping/ICMP echo, e.g. `P c0a80101 4` |
| `T` | `<hex-target> [max-hops]` (16) | Traceroute-style path probe (ICMP TTL sweep) |
| `I` | `<sec>` (30) | iperf2-compatible TCP server on `:5001` |
| `G` | `<pps> [sec]` (100 pps / 5 s) | Configurable-rate packet generator, TIM6-paced DMA descriptor ring, e.g. `G 1000 5` |
| `C` | `<sec>` (10) | Promiscuous capture to `/sniff.pcap` on the littlefs volume |
| `M` | `<sec>` (15) | MAC address table: switch-style learning with aging |
| `N` | `<sec>` (20) | L2 neighbor discovery: passive CDP/LLDP/STP + 802.1Q VLAN IDs seen |
| `R` | `<sec>` (20) | Rogue-DHCP watch: flags more than one distinct DHCPOFFER/ACK/NAK source |
| `B` | `<sec>` (20) | ARP watch: tracks IP→MAC bindings, flags any MAC change on an existing binding (a spoofing tell) |
| `U` | `<sec>` (20) | SSDP/UPnP watch: passive device/service discovery on UDP `:1900` |
| `O` | `<sec>` (20) | Traffic overview: destination class (broadcast/multicast/unicast) and EtherType mix, no per-source table — the zero-cost stateless sibling of `M`/`N` |
| `W` | `<hex-mac>` | Send a Wake-on-LAN magic packet, e.g. `W 0011223344AA` |

## Adapter GPIO and timing

| Cmd | Args | Does |
|---|---|---|
| `o` | `<hex>` | Drive adapter outputs OUT0..15 from a 16-bit hex mask |
| `F` | `<sec>` (5) | Frequency/period/duty-cycle counter on CN8 pin 5 (PB10, `TIM2_CH3/CH4`) |
| `D` | `<hz> <duty%> [sec]` (1000 Hz / 50% / 5 s) | PWM generator on OUT13 (PE5, `TIM9_CH1`), e.g. `D 1000 50` |
| `L` | `<hz> [sec]` (25 Hz / 5 s) | Logic analyzer: samples IN0..7/INT0..5 at a timer-triggered rate, renders a waveform on the panel |
| `K` | — | Continuity test: jumper OUT0 to INT0, drives low then high, reads INT0 back |

!!! note "`K` and `F`/`D` used together need a physical jumper"
    `K` and the input side of `F`/`L` only produce a meaningful *positive*
    result with an actual wire bridging the two adapter pins named — on a
    bench with nothing wired up, "no continuity" / "0 period samples" is the
    correct, well-formed report, not a failure of the command itself.

## Destructive

| Cmd | Args | Does |
|---|---|---|
| `z` | `FAULT` | Deliberately trips a UsageFault. **Halts for good — a reset or a reflash is the only way back.** Requires the literal argument `FAULT`; anything else is refused with an explanation, not silently ignored. |

This is the one command in the console that is destructive by design — it
exists to prove the fault handler itself works, not to be run casually.
Everything else here is read-only or bounded-duration by construction.
