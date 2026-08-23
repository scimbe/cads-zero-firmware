# toolbox

## What is it?

Fourteen small C11 files with nothing else in this project as a dependency.
Seven are generic utilities the rest of CaDS Zero kept re-inventing: a
lock-free byte ring buffer (`cads_ring`), integer formatting into a
caller-supplied buffer (`cads_fmt`), a TAP test-result writer (`cads_tap`),
bounded string helpers with strict parsers (`cads_str`), a publish/subscribe
list (`cads_pubsub`), a named service registry (`cads_record`) and leveled
logging (`cads_log`). It is the bottom of the dependency graph — it includes
nothing from this project, nothing from a vendor SDK, and nothing from the C
library.

The other seven back M5's and M6's bring-up "explorer" console commands with
the same no-HAL, no-lwIP, host-unit-tested split: six passive network
protocol parsers plus a caller-owned dedup/binding table each — `cads_l2discover`
(CDP/LLDP/STP + 802.1Q, command `N`), `cads_dhcpwatch` (rogue-DHCP-server
detection, command `R`), `cads_arpwatch` (ARP spoofing/cache-poisoning
detection, command `B`), `cads_ssdpwatch` (SSDP/UPnP discovery, command `U`),
`cads_mactable` (switch-style MAC learning with aging, command `M`) and
`cads_trafficstats` (stateless dest-class/EtherType tally, no table at all,
command `O`) — and one hardware-adjacent but still HAL-free timing utility,
`cads_freqcounter`, which turns a raw input-capture edge pair into a
period/frequency/duty-cycle measurement (command `F`) without depending on
which timer peripheral produced it. Each of the six parsers takes the same
caller-supplied byte buffer a promiscuous capture loop already fills for
`explorer_sniff_demo.c`, which is why they share
[the same capture buffer](../../apps/bringup/explorer_capture_buffer.h) at
the call site rather than each owning one. See
[the explorer console reference](../../docs/reference/explorer-console.md)
for what every command built on these actually does and prints.

## Why is it shaped this way?

**No allocation, no printf, no libc.** The STM32F429 has 192 KB of RAM, most of
which is already spoken for by a 75 KB framebuffer and lwIP. Linking newlib's
formatted output would cost upwards of 10 KB of flash and hundreds of bytes of
stack to print a pin number. Every function here writes into memory the caller
already owns; there is no `malloc`, no static scratch shared between calls, and
no recursion. That also makes each one usable from an interrupt.

**The ring is single-producer/single-consumer on purpose.** The producer is the
only writer of `head` and the consumer the only writer of `tail`, so a USART
receive ISR can fill a buffer that the main loop drains with no critical
section at all. Add a second producer and that guarantee is gone — use two
rings instead. The capacity is a power of two so the wrap is a mask, not a
division: one `AND` on the hot path. One slot is spent distinguishing full from
empty, so a 256-byte buffer holds 255 bytes.

**Overflow drops the newest byte and counts it.** A console line that loses its
tail is still recognisably truncated; one that loses its head is a different
command. `cads_ring_dropped()` exists so the loss is never silent — this is the
bug that made a truncated `b 90` set the backlight to 0% and report success.

**Formatting follows the `snprintf` contract** — always NUL terminated, returns
the length the complete output *would* have had — because that is the contract
every C programmer already checks correctly (`result >= size` means truncated).
Digits come out least-significant first, so each function builds into a small
automatic scratch buffer and copies out under the size limit rather than
leaving a half-written destination behind on truncation.

**The parsers reject rather than guess.** `cads_str_to_uint("")` returns false;
it does not return a plausible zero. Overflow is detected before it happens.

**`cads_pubsub` and `cads_record` exist to stop a layering violation before it
happens.** A board-only driver (say, the Ethernet PHY) and a portable app
(say, apps/netinfo) sometimes need to talk, but a portable app including a
`targets/` header is exactly the mistake `docs/reference/module-layout.md`
forbids, and it has already happened once for real
(`apps/bringup/explorer.c`). Publishing a state change, or registering an
instance under a name, lets the two sides meet without either header knowing
the other exists. Both are caller-owned-storage structures with no internal
locking — see the "not thread safe" note in each header for why, and what a
caller across two FreeRTOS tasks has to do about it.

**`cads_log` is the one module here with global rather than caller-owned
state**, and deliberately so: a log line has no natural handle to thread
through every call site the way `cads_tap_t` can afford to require (a TAP run
only ever happens once, at boot, single threaded). `cads_log_init()` wires a
sink once, early — the same write-through-a-callback pattern as `cads_tap`,
for the same reason: the identical call routes to the board's UART or the
host's stdout without an `#ifdef`. Levels follow syslog's ordering
(`CadsLogError` is 0, most severe); setting the minimum to `CadsLogWarn`
shows Error and Warn, drops Info and Debug.

**TAP writes through a callback.** The same test body runs on the board over
USART3 and on the host over `stdout` without a single `#ifdef`. Lines end CRLF
because the stream is read from a serial terminal, and because
`scripts/board_test.py` was written against exactly this format:

```
1..10
ok 1 - SysTick advances at 1 kHz
not ok 2 - DWT microsecond clock agrees
# systick_ms_over_50ms: 50
# 9/10 passed
# RESULT: FAIL
```

## How do I use it?

```c
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/ring.h"
#include "cads/toolbox/str.h"
#include "cads/toolbox/tap.h"

#include "cads_hal.h"

/* A console receive buffer an ISR fills and the main loop drains. */
static uint8_t rx_storage[256];
static cads_ring_t rx;

void console_isr(uint8_t byte) {
    cads_ring_push(&rx, byte); /* counts the byte if the reader fell behind */
}

/* TAP over whatever the HAL calls a console. */
static void write_console(void* context, const char* text, size_t length) {
    (void)context;
    cads_hal_console_write(text, length);
}

void selftest(void) {
    cads_ring_init(&rx, rx_storage, sizeof(rx_storage));

    cads_tap_t tap;
    cads_tap_init(&tap, write_console, NULL);
    cads_tap_plan(&tap, 2);

    cads_tap_check(&tap, cads_ring_capacity(&rx) == 255u, "ring holds 255 bytes");
    cads_tap_diag_uint(&tap, "dropped", cads_ring_dropped(&rx));

    /* "b 90" from the console, parsed strictly. */
    const char* line = "b 90";
    uint32_t percent = 0u;
    bool ok = cads_str_starts_with(line, "b ") &&
              cads_str_to_uint(line + 1, &percent, NULL) && percent <= 100u;
    cads_tap_check(&tap, ok, "backlight argument parses");

    char note[CADS_FMT_BUFFER];
    cads_fmt_uint_pad(note, sizeof(note), percent, 3, '0'); /* "090" */
    cads_tap_diag(&tap, note);

    cads_tap_finish(&tap); /* returns false if anything failed */
}
```

Link with `target_link_libraries(<your target> PRIVATE cads_toolbox)` and
include as `cads/toolbox/<name>.h`.

## What are the limits?

- **`cads_ring` is SPSC only.** Two producers, or two consumers, will corrupt
  it. It is also byte-oriented: there is no framing, so a reader that wants
  messages has to impose its own.
- **No floating point anywhere.** `cads_fmt` handles `uint32_t` and `int32_t`
  and nothing wider; 64-bit values must be split by the caller.
- **No general format strings.** There is no `%d` parser, so there is no
  format-string vulnerability and no varargs — call one function per field.
- **Padded output is capped at `CADS_FMT_MAX` (32) columns**, and a value wider
  than its field widens the field rather than losing a digit.
- **`cads_str` is byte-oriented, not UTF-8 aware**, and the comparisons are
  case sensitive.
- **`cads_tap` counts and formats; it does not schedule, name or discover
  tests.** It has no notion of a failing assertion aborting a run — the caller
  decides what to do with the `false` that `cads_tap_check()` returns.
- **`cads_pubsub` delivery order is unspecified**, and a callback that
  subscribes or unsubscribes its own pubsub while being called is not
  supported — the list is walked live.
- **`cads_record` names are capped at `CADS_RECORD_NAME_MAX` (15 bytes)** and
  a name that does not fit is refused outright, not truncated - two different
  modules truncating to the same prefix would collide silently otherwise.
- **`cads_log` has one global sink, not one per caller**, and is not
  thread-safe for the same reason `cads_pubsub`/`cads_record` are not - see
  the header. It also carries no timestamp: attaching one would mean calling
  a HAL clock function this module has no business depending on, so a caller
  that wants one puts it in the message text.
