# `modules/cli` — one command table, two transports

## What is it?

A small line-oriented command interpreter. `cads_cli_session_t` is caller-owned
state fed one byte at a time through `cads_cli_session_feed()`; once a `\r` or
`\n` completes a line, the session looks the first word up in a single static
command table compiled into `cads_cli.c` (`help`, `version`, `uptime`, `net`,
`echo`) and calls its handler with the rest of the line as `args`. Every
handler writes back through the same `cads_cli_write_fn` the session was
initialised with, so the same five commands work identically whichever byte
stream is driving them. Two things currently drive a session:
`apps/bringup/explorer_cli_demo.c` (the hardware explorer's `j` command) feeds
one straight from `cads_hal_console_read()`, and `cads_cli_tcp_start()` — board
only, `port` chosen by the caller (`explorer_cli_demo.c` picks 4242) — accepts
one lwIP connection at a time and feeds a second session from its `tcp_recv`
callback. Both run at once when `j` is used: the same 30-second window is
reachable over the serial console *and* over `telnet <board-ip> 4242`.

## Why is it shaped this way?

**A byte-feed function, not a line-read function.** Neither transport this
module actually has hands a complete line: a UART ISR/poll loop hands over one
byte, and `cads_cli_tcp_recv()` hands over a `pbuf` chain that it already walks
one byte at a time into `cads_cli_session_feed()`. Putting the buffering
inside the session means both transports share the exact same line-assembly
and error-handling path instead of each inventing its own.

**One command table: built-ins compiled in, plus a few registered slots.**
The header's own comment states the split plainly: neither transport knows
what a command does, and this file knows nothing about UARTs or sockets — the
same "transport vs. logic" separation `modules/net`'s `cads_net_board.c` /
`cads_net_sim.c` split already established. The built-ins stay a `static`
array in `cads_cli.c`; an app that needs its own command (`apps/rnlab`'s
`lab`) calls `cads_cli_register()` once at init with a pointer to its own
`static const cads_cli_command_t`, and from then on every transport
dispatches it. Only the pointer is stored (`CADS_CLI_REGISTERED_MAX` = 4
slots), and a name that is already taken — built-in or registered — is
refused rather than shadowed.

**Command lookup is exact-match, not prefix-match.** `cads_cli_dispatch()`
checks both `cads_str_compare_n(name, cmd.name, name_length) == 0` *and*
`cmd.name[name_length] == '\0'` — the second check is what stops a typed `he`
from silently landing on `help`. A diagnostic tool that guesses which command
you meant is worse than one that says `? unknown command`.

**`CADS_CLI_LINE_MAX` is 96, and an overlong line is discarded whole, not
truncated.** The header ties this directly to `apps/bringup/explorer.c`'s own
`char line[32]` reasoning: anything needing more belongs in its own binary
transfer, not a text command. Truncating instead of discarding would produce
exactly the bug `modules/toolbox/include/cads/toolbox/str.h` documents by
name — a command that silently turns into a different, shorter, still-valid
one. `cads_cli_session_feed()` resets `length` to 0 and reports `? line too
long, discarded` rather than dispatch a mutilated line.

**`cads_cli_write_fn` must never block, and every write site honours that.**
`cads_cli_tcp_write()` checks `tcp_sndbuf(pcb)` and drops the output outright
when it is 0, clamping to whatever room is left otherwise — because this
callback fires synchronously from inside `cads_net_poll()`'s call chain
(`cads_netif.input() → … → tcp_input()`, per that file's own header comment),
and blocking there would stall the netif for every other consumer of it, not
just the CLI.

**One TCP connection at a time, by construction, not just by policy.**
`cads_cli_tcp_board.c` holds exactly one `cads_cli_tcp_conn_t` as a file-scope
static — there is no array, no allocation, so a second real connection has
nowhere to be stored even before `cli_tcp.h`'s stated policy ("a diagnostic
tool for one operator, not a multi-user shell") comes into it. A second
concurrent attempt is accepted at the TCP level and then immediately closed
in `cads_cli_tcp_accept()`, rather than letting two sessions dispatch into the
same command table's side effects unsynchronised.

**The TCP transport is board/sim-split; `cads_cli.c` itself is not.** The
`CMakeLists.txt` comment spells out why: `cads_cli.c` is fully portable
(target-neutral, same as `cads/net/net.h`) and builds identically for both
targets, but the socket layer needs lwIP, so it follows the same
`cads_net_board.c`/`cads_net_sim.c` split net already established —
`cads_cli_tcp_board.c` on `CMAKE_SYSTEM_NAME STREQUAL "Generic"`,
`cads_cli_tcp_sim.c`'s honest `return false` everywhere else.

**`cads_cli` reaches into `core/` privately for one function.** It needs
`cads_hal_ticks_ms()` for `uptime`, but neither `cads_net` nor `cads_toolbox`
expose `core/` publicly (`cads_net`'s own include of it is `PRIVATE`;
`cads_toolbox` never needed it at all) — so `CMakeLists.txt` gives `cads_cli`
its own `PRIVATE` include path to `core/` rather than depending on either
module to leak it transitively.

**Not the hardware explorer.** `cli.h`'s header comment is explicit that this
is a second, smaller, general-purpose command set — five commands versus the
explorer's ~30 hardware-verified single-letter ones — reachable over the
network as well as serial, which folding into the existing tool would not be.

## How do I use it?

```c
#include "cads/cli/cli.h"
#include "cads/cli/cli_tcp.h"
#include "cads/net/net.h"
#include "cads_hal.h"

static void console_write(void* context, const char* text, size_t length) {
    (void)context;
    cads_hal_console_write(text, length);
}

void run_cli(const uint8_t mac[6], uint32_t seconds) {
    cads_net_init(mac);                       /* idempotent; needed for TCP */
    cads_cli_tcp_start(4242u);                /* false on the simulator - see cli_tcp.h */

    cads_cli_session_t session;
    cads_cli_session_init(&session, console_write, NULL);
    cads_cli_write(&session, "CaDS Zero CLI - 'help' for commands\r\n> ");

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_net_poll(); /* pumps the TCP session's recv/error callbacks */

        uint8_t byte;
        if(cads_hal_console_read(&byte)) {
            cads_cli_session_feed(&session, byte); /* serial half of the session */
        } else {
            cads_hal_delay_ms(2u);
        }
    }
}
```

Link with `target_link_libraries(<your target> PRIVATE cads_cli)`; it brings
in `cads_net` and `cads_toolbox` publicly. `cads_cli_tcp_start()` needs no
per-connection code from the caller at all — `cads_net_poll()` alone is enough
to drive an already-accepted TCP session, since `cads_cli_tcp_accept()` wires
up `cads_cli_session_init()` internally.

## What are the limits?

- **Five built-in commands plus at most `CADS_CLI_REGISTERED_MAX` (4)
  registered ones.** `help`, `version`, `uptime`, `net`, `echo` are compiled
  in; `cads_cli_register()` adds more at runtime and never removes them
  again. `cads_cli_execute()` dispatches an already-assembled line (the
  hardware explorer's serial loop uses it for `lab ...`).
- **96-byte line cap, whole-line discard on overflow** — see above. Long
  arguments (a file path, a hex blob) do not fit and are not meant to.
- **One TCP connection at a time, board only.** `cads_cli_tcp_start()` always
  returns `false` on the simulator (`cads_cli_tcp_sim.c`) — there is no
  network stack there to bind a socket on.
- **Bounded output buffering on TCP.** `cads_cli_write_fn` still never
  blocks. What `tcp_sndbuf()` cannot take right now waits in a 1 KB queue
  (`cads/cli/cli_stream.h`, storage in CCM) and is sent from `tcp_sent` as
  ACKs free room; only when that queue is full too is the rest dropped, and
  the operator then gets `? Ausgabe gekuerzt` once the queue has drained.
  Waiting in place is impossible: commands run inside `cads_net_poll()`,
  which is also what processes the ACKs.
- **Line ends and telnet.** CR LF and CR NUL count as one line end (one
  prompt per line from PuTTY, Windows telnet, `nc`); telnet IAC command
  sequences are filtered out of the TCP input and never answered, so a real
  telnet client's option negotiation does not reach the command parser.
- **`cads_cli_write()` only takes NUL-terminated C strings**, and only
  examines the first `CADS_CLI_LINE_MAX * 4` (384) bytes of one — there is no
  variant that takes an explicit length or writes binary data.
- **No authentication, no encryption on the TCP listener.** Anything that can
  reach the board's IP on the chosen port gets a prompt; `cads_cli_tcp_start()`
  accepts unconditionally once the listener is up (rejecting only a *second*
  concurrent connection). It is a bring-up diagnostic tool, not something to
  expose past a trusted network.
- **No persistence, no scripting, no line editing.** Every line is independent;
  there is no history, no `up-arrow`, no multi-line input, and nothing here
  remembers a previous command.
