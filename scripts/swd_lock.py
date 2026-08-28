#!/usr/bin/env python3
"""Serialize every SWD (ST-Link debug-port) operation on this host.

Why this exists (issue #57, 2026-08-27): two clients talking to one ST-Link
at the same time desync its USB protocol state machine - every later command
times out (LIBUSB_ERROR_TIMEOUT, chipid 0x000) until the ST-Link is
physically replugged. In the worst case an interleaved or aborted command
stream mis-programs flash (observed: a single cleared bit in the vector
table's initial-SP word at 0x08000000 - the board hung in Reset_Handler with
its stack pointing into the flash alias). Overlap happens easily in
automation: a background st-flash read still running while a foreground
st-info probes, or a shell timeout SIGKILLing st-flash mid-bulk-transfer.

Usage:
    from swd_lock import swd_lock
    with swd_lock():
        subprocess.run(["st-flash", ...], check=True)

The lock is advisory and host-wide (one file in the user's temp dir), held
for the duration of the with-block. Cross-platform: fcntl on POSIX, msvcrt
on Windows. Waiting is unbounded on purpose - an st-flash write of the
896 KB filesystem region takes tens of seconds and must never be aborted
mid-write; if you think the lock is stuck, find and finish (not kill) the
holder first.
"""
import contextlib
import os
import sys
import tempfile

LOCK_PATH = os.path.join(tempfile.gettempdir(), "cads-swd.lock")


@contextlib.contextmanager
def swd_lock():
    fd = os.open(LOCK_PATH, os.O_RDWR | os.O_CREAT, 0o666)
    try:
        if os.name == "nt":
            import msvcrt
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
            try:
                yield
            finally:
                os.lseek(fd, 0, os.SEEK_SET)
                msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(fd, fcntl.LOCK_UN)
    finally:
        os.close(fd)


if __name__ == "__main__":
    # shell wrapper: swd_lock.py st-flash --serial X write fw.bin 0x08000000
    import subprocess
    if len(sys.argv) < 2:
        sys.exit("usage: swd_lock.py <command> [args...]")
    with swd_lock():
        sys.exit(subprocess.run(sys.argv[1:]).returncode)
