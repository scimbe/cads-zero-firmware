# Send your first UDP packet

From a stock build to one real UDP datagram, sent by the board's own lwIP
stack and landing in a terminal on your laptop. Twenty minutes.

**Prerequisites:** an ITSboard already built and flashed (see [Build and
flash your first image](first-build.md)), an Ethernet cable from the
board's jack straight to your laptop (or through a switch — no DHCP server
needed either way), and `nc` (netcat) — installed by default on macOS and
most Linux distributions.

## 1. Give your laptop an address on the board's subnet

Out of the box the board's addressing is **static**, not DHCP —
`cads_net_board.c`'s built-in default is `192.168.33.99/24`, gateway
`192.168.33.1` (see the [config reference](../reference/config-file.md)).
Zero config changes needed. Your laptop just needs an address in the same
`/24` on whichever interface is wired to the board.

On macOS, a temporary alias on top of whatever `en0` already has:

```bash
sudo ifconfig en0 alias 192.168.33.10 255.255.255.0
```

Any free address in `192.168.33.0/24` works — this tutorial uses `.10`
throughout. Exact steps differ on Windows/Linux; the requirement is the
same one address, reachable from your NIC.

## 2. Start listening

```bash
nc -ul 41234
```

It prints nothing and blocks — that's correct, it's waiting. `41234` is an
arbitrary high port; picked to stay clear of `37008`, which
[the Marauder PCAP relay](wifi-wireshark.md) already uses for something
else in this codebase.

Leave this running. Everything from here happens in a second terminal.

## 3. Add one call to a real demo

There's no built-in "send one packet" command, but
`apps/bringup/explorer_ping_demo.c` already does everything except the
send: `cads_net_init()`, then the link-wait loop every real caller in this
codebase uses (`cads_net_status()` only reports the last poll's cached
result — nothing detects link-up on its own, you have to poll for it).

Open the file. Right after the link-wait loop, before `char
target_text[16];`, add:

```c
    /* lwip-udp-hello: one UDP datagram to the laptop from step 1 */
    uint32_t laptop_ip = (192u << 24) | (168u << 16) | (33u << 8) | 10u; /* 192.168.33.10 */
    static const uint8_t hello_payload[] = "hello from cads-zero\n";
    cads_net_udp_send(laptop_ip, 41234u, hello_payload, sizeof(hello_payload) - 1u);
```

That's the entire lesson: `dst_ip`/`dst_port` in host byte order, no
socket object, no return value to check. Per
`modules/net/include/cads/net/net.h`'s own doc comment on
`cads_net_udp_send()`, it silently no-ops if the link isn't up or
`dst_ip` is 0 — the same fire-and-forget shape `apps/marauder`'s PCAP
relay already relies on for its one real call site. If your laptop isn't
`.10`, change the last octet to match step 1.

## 4. Build for both targets

Everything above the HAL has to build for the board and the simulator —
`cads_net_udp_send()` has a host stub (`cads_net_sim.c`) that compiles and
links but never actually sends, since the simulator has no link to send
over.

```bash
cmake --build build/itsboard
cmake --build build/host
```

Both should finish clean. The host build won't send anything when run —
that's expected, not a bug to chase.

## 5. Flash

```bash
scripts/flash.sh
```

```
Flashing build/itsboard/cads-zero.bin (... bytes) to 0x08000000
EraseFlash - Sector:0x0 Size:0x4000 -> Flash page at 0x8000000 erased
...
Done.
```

## 6. Fire it

If this is a fresh flash, the board booted straight into the touchscreen
app-tree menu (`boot.autostart=1`) — and that session ignores plain typed
bytes entirely, on purpose (see `scripts/board_key.py --help`), so a
console command sent into it does nothing and prints nothing, not even an
error. One-time fix, only needed if you haven't already left that screen:

```bash
scripts/board_key.py quit
```

Now the real command — note the ping target and count are one quoted
argument, not two (the board itself parses `"<hex-ip> <count>"` out of a
single string):

```bash
scripts/board_cmd.py P "c0a8210a 1" --timeout 5
```

`P` is the existing ping command; `c0a8210a` is `192.168.33.10` in hex,
`1` is the ping count. The UDP hello fires once the link-wait loop breaks —
before the ping loop even starts — so it goes out regardless of whether
your OS answers the ping that follows it.

Check the `nc` terminal from step 2:

```
hello from cads-zero
```

That line arriving is the whole exercise: a real datagram, built by lwIP
on the board, crossed the wire, and a plain socket on your machine read
it with no decoding step in between.

**If the ping itself reads `request timed out`**, that's fine — most
consumer firewalls or a laptop that just doesn't answer ICMP will do that.
It says nothing about the UDP hello, which already left before that loop
ran.

## If nothing arrives

| Symptom | Likely cause |
|---|---|
| `nc` prints nothing at all | Laptop's alias isn't actually on the wire the board is plugged into, or is on the wrong subnet — recheck step 1 |
| Console shows the demo ran, still nothing in `nc` | `laptop_ip`'s last octet doesn't match the alias you set |
| Console never gets past the link-wait (3 s, no output at all) | Cable, or the board came up with `net.dhcp = 1` from a prior session — [check `/config.txt`](../how-to/configure.md) |
| `nc: Address already in use` | Something else already owns port 41234 — pick a different one in both places |
| `board_cmd.py` prints absolutely nothing, not even an error | The board is still sitting in the app-tree menu — run `scripts/board_key.py quit` first, then retry the `P` command (step 6) |

None of these are board-side bugs to debug over SWD — every one is either
a laptop-networking mismatch or a typo in step 3.
[Debug with GDB](../how-to/debug.md) is the next step only if the board
itself looks wrong (crashes, resets, no console at all).

## Next

**[WiFi recon and live capture in Wireshark](wifi-wireshark.md)** — the
same netif, but relaying a real capture stream instead of one hand-written
datagram.
