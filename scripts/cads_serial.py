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

import os
import selectors
import termios
import time


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
