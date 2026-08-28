# Configuration file and build profiles

Two different text files, deliberately kept apart. `/config.txt` lives on the
board and changes at runtime; a `*.profile` lives in the repo and changes at
build time. This page is the complete reference for both formats and the tools
that read them.

## `/config.txt` — runtime configuration

A plain-text `key = value` file in the littlefs volume (flash bank 2,
`0x08120000`). The firmware reads it at boot; if it is missing it writes the
built-in defaults so the file always exists to edit. `#` starts a comment;
blank lines are ignored; whitespace around keys and values is trimmed. An
unrecognised key is skipped, and any key the file omits keeps its default —
so a hand-truncated or partial file degrades to "defaults plus whatever
parsed", never a garbage state.

### Keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `boot.autostart` | bool | `1` | Boot straight into the menu (the panel is usable standalone). Any console key drops back to the explorer prompt; that key is not lost — it becomes the first character of the next command. `0` boots to the console prompt, the pre-autostart behavior. |
| `display.brightness` | 0–100 | `80` | Backlight percent. Values above 100 clamp to 100. |
| `display.fast_clock` | bool | `0` | Display SPI divider: `1` = fast (/8), `0` = safe (/16). |
| `net.dhcp` | bool | `0` | `1` requests a DHCP lease; `0` uses the static fields below. |
| `net.ip` | IPv4 | `192.168.33.99` | Static host address (ignored when `net.dhcp = 1`). |
| `net.netmask` | IPv4 | `255.255.255.0` | Static subnet mask. |
| `net.gateway` | IPv4 | `192.168.33.1` | Static default gateway. |
| `wifi.enabled` | bool | `0` | Gates Settings → **Join WiFi**: `1` and a non-empty `wifi.ssid` let that row start a join against the ESP32Marauder co-processor (see [marauder-coprocessor.md](marauder-coprocessor.md)). |
| `wifi.ssid` | string ≤32 | *(empty)* | The SSID Settings → Join WiFi scans for and joins via Marauder's `join -a` (index-based — there is no CLI command to set an SSID by name, see `cads_marauder_join()`'s own doc comment). |
| `wifi.password` | string ≤63 | *(empty)* | Password for `wifi.ssid`. |
| `wifi.uart` | string | `usart6` | Which UART the co-processor is wired to. Not yet read by the driver (`board.h`'s `CADS_WIFI_*` constants are the actual source of truth today) — carried for a future multi-UART board. |
| `wifi.pcap_target` | IPv4 | `0.0.0.0` | Where the Marauder tool's **Sniff (PCAP)** live relay sends TZSP-encapsulated captured frames (UDP port 37008, Wireshark's own `udpdump` default — see [marauder-pcap-stream.md](marauder-pcap-stream.md)). `0.0.0.0` (unset) means the relay parses and counts frames but sends nothing. |

Booleans accept `1`/`0`, `on`/`off`, `true`/`false`, `yes`/`no`. IPv4 values
are dotted decimal; an out-of-range octet or a malformed address is rejected
and the key keeps its previous value.

### The base version

```ini
# CaDS Zero configuration
# Edit and save, then reload from Settings -> Reload config.

# boot
boot.autostart = 1

# display
display.brightness = 80
display.fast_clock = 0

# network
net.dhcp = 0
net.ip = 192.168.33.99
net.netmask = 255.255.255.0
net.gateway = 192.168.33.1

# wifi (ESP32Marauder co-processor - see docs/reference/marauder-coprocessor.md)
wifi.enabled = 0
wifi.ssid =
wifi.password =
wifi.uart = usart6
wifi.pcap_target = 0.0.0.0
```

### Applying changes

`display.*`, `net.*` and `wifi.*` are all applied live by **Settings →
Reload config** — no reboot. They are also applied once at startup.
`wifi.ssid`/`wifi.password` only take effect when Settings → **Join WiFi**
is actually selected (Reload config alone does not trigger a join, it just
updates what a later Join WiFi row would use); `wifi.pcap_target` takes
effect immediately — the Marauder tool view's PCAP relay reads it on every
reload/startup, same as `net.*`.

## `*.profile` — build-time feature selection

A profile picks which optional apps compile into the image. Format is one
`app.<name> = on|off` per line; `#` comments and blank lines allowed. It is
read once, by CMake, at configure time — it has nothing to do with the flash
filesystem or a running board.

### Recognised apps

`settings`, `about`, `gpio`, `netinfo`, `filebrowser`, `game`, `netiperf`,
`nettools`, `active`. These map to the `CADS_APP_*` CMake options. `desktop`
and the menu shell are always built and are not profile keys.

An unknown app name or a malformed line is a hard configure error, not a
silent skip — a typo must not quietly build the wrong image.

### Precedence

With a profile active, **the profile is authoritative** for the apps it names.
It sets those `CADS_APP_*` values with `FORCE`, so switching or editing a
profile in an existing build directory takes effect on the next configure (the
profile file is a configure dependency — editing it re-runs CMake). To override
one app, edit the profile; a `-DCADS_APP_X=` on the command line does **not**
beat an active profile.

Without a profile (`CADS_PROFILE` empty), a `-DCADS_APP_X=` overrides the
built-in `ON` default as usual. Order: **profile (if set)** → **command-line
`-D`** → **built-in `ON` default**.

### Shipped profiles

| Profile | Apps on | Notes |
|---|---|---|
| `profiles/full.profile` | all 9 | Same as omitting `CADS_PROFILE`. |
| `profiles/minimal.profile` | `settings` only | Smallest useful image; ~16.5 KB RAM margin vs the full build's ~9.8 KB. |

## Tools

### On the board

- **Settings → Reload config** re-reads `/config.txt` and applies it live.

### On a host (macOS / Linux / Windows)

The board exposes no USB mass storage — only SWD — so editing the file means
dumping the filesystem, changing one file in the image, and writing it back.
`scripts/cads_config.py` wraps that:

```bash
scripts/cads_config.py pull [-o FILE]   # save the board's config.txt locally
scripts/cads_config.py push FILE        # write FILE onto the board
scripts/cads_config.py edit             # pull, open $EDITOR, push if changed
```

It uses `cads_fs` (built from `build/host`, the same littlefs code the
firmware runs) and `st-flash`. After a push, apply with **Settings → Reload
config** or power-cycle.

`cads_fs` can be driven directly against any dumped image:

```bash
cads_fs <image> get /config.txt [out-file]
cads_fs <image> put /config.txt <in-file>
```

### Validating a profile

```bash
scripts/check_profile.py profiles/minimal.profile          # syntax + view-capacity check
scripts/check_profile.py profiles/minimal.profile --build  # + a real build and RAM check
```

The capacity check compares the view count the profile's enabled apps would
register against `CADS_APP_DEMO_VIEW_CAPACITY` — the fixed-size registry that
silently drops views past capacity — so a profile that would ship a dead menu
row fails validation instead of the board.

## See also

- [Why the config and build profiles are two separate files](../explanation/config-design.md) — the design discussion.
- [How to configure the firmware](../how-to/configure.md) — the task walkthrough.
- [Memory map](memory-map.md) — where the littlefs volume sits.
