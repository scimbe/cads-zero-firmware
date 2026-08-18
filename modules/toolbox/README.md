# toolbox

## What is it?

Four small, generic C11 utilities that the rest of CaDS Zero kept re-inventing:
a lock-free byte ring buffer (`cads_ring`), integer formatting into a
caller-supplied buffer (`cads_fmt`), a TAP test-result writer (`cads_tap`), and
bounded string helpers with strict parsers (`cads_str`). It is the bottom of the
dependency graph — it includes nothing from this project, nothing from a
vendor SDK, and nothing from the C library.

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
