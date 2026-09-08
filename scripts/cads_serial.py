"""CaDS Zero - dependency-free serial console reader.

Why this exists instead of a two-line pyserial call: the board tests have to
run on a bare checkout with nothing but a system Python, and adding a pip
dependency to the hardware gate is a good way to make the gate get skipped.

The subtlety that this module exists to get right:

    stty -f /dev/cu.usbmodemXXXX 115200 raw

does NOT do what it looks like it does. On macOS `stty -f` opens the device,
applies the settings and closes it again - and closing the last descriptor
resets the line discipline. A subsequent `cat` therefore reads at the driver
default of 9600, no matter what was just requested. The symptom is maddening:
identical garbage at every baud rate you try, which looks like a hardware fault
and is not.

The fix is to open the port once and configure termios on that same descriptor,
which is what open_console() does.
"""

from __future__ import annotations

import glob
import json
import os
import selectors
import sys
import termios
import time
import urllib.error
import urllib.request

# The firmware-lab course container's board-bridge extension (a separate
# repo/extension, not this one) exposes the board over a browser WebUSB/
# WebSerial connection instead of a host /dev/cu.usbmodem* device. It relays
# that connection to two loopback-only endpoints inside the container:
#   - a real PTY at DEFAULT_BRIDGE_CONSOLE_LINK (socat links it to the
#     bridge's own TCP serial server), so it behaves exactly like a real
#     tty - open_console()/termios/read_lines() below need no changes to use
#     it, only the path differs from a real VCP.
#   - an HTTP status/control shim on DEFAULT_BRIDGE_HTTP_PORT, whose
#     GET /status tells you whether a browser tab actually has the board
#     connected. This matters because the bridge's serial write path accepts
#     bytes and reports success even with no board attached (the rejection is
#     only logged, not surfaced) - so silence on the console does not by
#     itself mean the write failed, and resolve_console_port() below checks
#     /status specifically so a script doesn't misread "PTY exists" as
#     "board is there and listening".
DEFAULT_BRIDGE_CONSOLE_LINK = "/home/coder/board-console"
DEFAULT_BRIDGE_HTTP_PORT = 3335


class SerialTimeout(Exception):
    """Raised when the board produced nothing within the allotted time."""


def open_console(port: str, baud: int = 115200) -> int:
    """Open a serial port raw at `baud` and return the file descriptor.

    O_NONBLOCK matters at open() time as well as afterwards: a cu.* device
    will otherwise block until carrier detect, which some adapters never
    assert.
    """
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        speed = getattr(termios, f"B{baud}")
    except AttributeError as exc:  # pragma: no cover - configuration error
        os.close(fd)
        raise ValueError(f"unsupported baud rate {baud}") from exc

    # Fully raw: no character translation, no echo, no signal characters. Any
    # of those would silently mangle a TAP stream containing '\r'.
    iflag = 0
    oflag = 0
    lflag = 0
    cflag = termios.CREAD | termios.CLOCAL | termios.CS8

    control = list(termios.tcgetattr(fd)[6])
    control[termios.VMIN] = 0
    control[termios.VTIME] = 0

    termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, speed, speed, control])

    # Drop whatever was sitting in the driver buffer from a previous run so the
    # caller only sees output produced after this point.
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def read_lines(fd: int, timeout: float, stop_when=None, echo: bool = True):
    """Yield decoded lines from `fd` until `stop_when` matches or time runs out.

    `timeout` is a hard wall-clock deadline for the whole call, not a per-read
    idle timeout. Hardware misbehaves; a gate that can hang forever is worse
    than one that fails.
    """
    selector = selectors.DefaultSelector()
    selector.register(fd, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    buffer = b""

    try:
        while time.monotonic() < deadline:
            for _key, _mask in selector.select(timeout=min(0.25, max(0.0, deadline - time.monotonic()))):
                chunk = os.read(fd, 4096)
                if not chunk:
                    continue
                buffer += chunk
                while b"\n" in buffer:
                    raw, buffer = buffer.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").rstrip("\r")
                    if echo:
                        print(f"  | {line}", flush=True)
                    yield line
                    if stop_when is not None and stop_when(line):
                        return
    finally:
        selector.unregister(fd)
        selector.close()


def find_local_vcp() -> str | None:
    """First numeric /dev/cu.usbmodem* device, or None if there isn't one.

    The ST-Link VCP enumerates with a purely numeric suffix; other CDC
    devices (an LG monitor's control interface, say) carry letters.
    """
    candidates = [c for c in sorted(glob.glob("/dev/cu.usbmodem*"))
                  if c.rsplit("usbmodem", 1)[1].isdigit()]
    return candidates[0] if candidates else None


def bridge_status(http_port: int = DEFAULT_BRIDGE_HTTP_PORT, timeout: float = 2.0) -> dict | None:
    """GET /status from the board-bridge's HTTP shim, or None if unreachable.

    Only meaningful inside the firmware-lab course container - a bare
    checkout with a directly-attached board has no bridge to ask, and that
    is not an error, just "not applicable here".
    """
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{http_port}/status", timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, OSError, ValueError):
        return None


def resolve_console_port(explicit: str | None = None,
                          bridge_console_link: str = DEFAULT_BRIDGE_CONSOLE_LINK,
                          bridge_http_port: int = DEFAULT_BRIDGE_HTTP_PORT) -> str:
    """Pick the serial device to open, in priority order, or exit with a clear reason.

    1. `explicit` (a --port flag) or $CADS_CONSOLE_PORT - always wins, no checks.
    2. A local ST-Link VCP (/dev/cu.usbmodem*, numeric suffix) - the case for
       a Mac with the board plugged in directly.
    3. The board-bridge's PTY link - the case inside the firmware-lab course
       container, where the board is reached through the browser's WebUSB/
       WebSerial connection instead of a host serial device. socat links this
       path to a real tty, so everything downstream (open_console(), termios,
       read_lines()) works completely unchanged; only the path differs.

    Before trusting the PTY, this checks the bridge's own GET /status: the
    bridge accepts a /serial write and reports success even with no board
    connected (the rejection is only logged on their side, never surfaced),
    so a script that just opened the PTY and started writing could easily
    mistake "the escape hatch exists" for "the escape hatch reaches a board".
    If /status says no board is connected, this fails loudly with the
    reported reason instead of returning a PTY that will eat commands
    silently - that silent-eating is exactly the failure PB-03 was about.
    """
    port = explicit or os.environ.get("CADS_CONSOLE_PORT")
    if port:
        return port

    local = find_local_vcp()
    if local:
        return local

    if os.path.exists(bridge_console_link):
        status = bridge_status(bridge_http_port)
        if status is not None and not status.get("connected", False):
            reason = (status.get("probe") or {}).get("blockReason") or "no board connected"
            sys.exit(
                f"board-bridge console PTY exists ({bridge_console_link}) but the bridge "
                f"reports no board connected: {reason} - use 'CaDS Board: Verbinden "
                "(USB/Serial freigeben)' in this course's IDE first."
            )
        return bridge_console_link

    sys.exit(
        "no ST-Link VCP found (no numeric /dev/cu.usbmodem*) and no board-bridge "
        f"console PTY at {bridge_console_link} - pass --port or set CADS_CONSOLE_PORT. "
        "Inside the firmware-lab course container this usually means socat hasn't "
        "created the link yet (it restarts with up to 30s backoff; check the "
        "'CaDS Board' output channel for 'socat not found' if it never appears) "
        "or no browser tab has connected the board yet."
    )
