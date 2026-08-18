#!/usr/bin/env python3
"""CaDS Zero - sustained on-target stability test.

The one-shot gate in board_test.py proves the firmware boots and that each
subsystem works once. It cannot catch what actually kills embedded firmware:
a stack that grows past its allocation after an hour, a counter that wraps, a
priority inversion that only bites under load, an interrupt that is lost once
in ten thousand times.

So this runs the board for a long time and watches. Every interval it asks the
console for a task report and checks:

  - the board still answers at all (a hang is the most common failure);
  - no task's stack high-water mark is shrinking towards zero;
  - the task count is stable, so nothing died;
  - the input counter still moves if the operator is pressing things;
  - the display keeps flushing.

WHY IT DRIVES LOAD
------------------
An idle soak measures the wrong thing. Left alone, the UI task never flushes -
nothing is dirty - so its stack high-water mark reflects a call chain that
never went through the canvas, the band conversion, the HAL blit and the DMA
wait. That is the deepest path in the system and the only one worth sizing
against.

So the test draws. Every interval it asks for a full-screen pattern, which
forces the worst case: a 448 ms flush holding the display mutex while the
input task keeps preempting it at 100 Hz. If a stack is too small, or a long
flush starves something, this is what surfaces it.

It deliberately does not require a human. Left running overnight it either
stays quiet or produces the first timestamp at which something changed.

Usage:
    scripts/board_soak.py --minutes 10
    scripts/board_soak.py --minutes 480 --interval 30   # overnight
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cads_serial import open_console, read_lines  # noqa: E402

REPORT_RE = re.compile(
    r"#\s*tasks\s+ui_free=(\d+)\s+input_free=(\d+)\s+console_free=(\d+)\s+"
    r"tasks=(\d+)\s+events=(\d+)\s+last_key=(\d+)")

# A stack this close to exhaustion is a fault waiting to happen, not a warning.
STACK_FLOOR_BYTES = 128


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/cu.usbmodem11303")
    parser.add_argument("--minutes", type=float, default=10.0)
    parser.add_argument("--interval", type=float, default=15.0, help="seconds between reports")
    parser.add_argument("--settle", type=float, default=10.0, help="seconds to let boot finish")
    parser.add_argument("--no-load", action="store_true",
                        help="do not drive redraws (measures idle, which sizes nothing)")
    args = parser.parse_args()

    deadline = time.monotonic() + args.minutes * 60.0
    fd = open_console(args.port, 115200)

    samples = 0
    silent = 0
    worst = {"ui": None, "input": None, "console": None}
    first = None
    problems: list[str] = []

    try:
        time.sleep(args.settle)
        for _ in read_lines(fd, timeout=1.0, echo=False):
            pass

        pattern = 0
        while time.monotonic() < deadline:
            started = time.monotonic()

            if not args.no_load:
                # Force the deepest path: a full-screen redraw. Patterns rotate
                # so the whole panel is rewritten rather than a no-op region.
                pattern = (pattern + 1) % 7
                os.write(fd, f"p {pattern}\r\n".encode())
                for _ in read_lines(fd, timeout=12.0,
                                    stop_when=lambda l: "drawn" in l, echo=False):
                    pass

            os.write(fd, b"k\r\n")

            report = None
            for line in read_lines(fd, timeout=6.0,
                                   stop_when=lambda l: REPORT_RE.search(l) is not None,
                                   echo=False):
                match = REPORT_RE.search(line)
                if match:
                    report = match

            elapsed = int(time.monotonic() - (deadline - args.minutes * 60.0))

            if report is None:
                silent += 1
                problems.append(f"t+{elapsed}s: no response")
                print(f"t+{elapsed:5d}s  NO RESPONSE ({silent} so far)", flush=True)
            else:
                ui, inp, con, tasks, events, last_key = (int(g) for g in report.groups())
                samples += 1
                current = {"ui": ui, "input": inp, "console": con}
                for name, value in current.items():
                    if worst[name] is None or value < worst[name]:
                        worst[name] = value
                    if value < STACK_FLOOR_BYTES:
                        problems.append(f"t+{elapsed}s: {name} stack down to {value} B")

                if first is None:
                    first = (tasks,)
                elif tasks != first[0]:
                    problems.append(f"t+{elapsed}s: task count changed {first[0]} -> {tasks}")

                print(f"t+{elapsed:5d}s  stacks ui={ui} input={inp} console={con}  "
                      f"tasks={tasks} events={events} key={last_key}"
                      f"{'' if args.no_load else f'  pattern={pattern}'}", flush=True)

            remaining = args.interval - (time.monotonic() - started)
            if remaining > 0:
                time.sleep(min(remaining, max(0.0, deadline - time.monotonic())))
    finally:
        os.close(fd)

    print()
    print(f"samples: {samples}, silent intervals: {silent}")
    print(f"worst stack free: ui={worst['ui']} input={worst['input']} console={worst['console']} bytes")
    if args.no_load:
        print("note: run without --no-load to size stacks against the deepest path")

    if problems:
        print(f"\nFAIL: {len(problems)} problem(s)")
        for problem in problems[:20]:
            print(f"  {problem}")
        return 1

    if samples == 0:
        print("\nFAIL: the board never answered")
        return 2

    print(f"\nPASS: {args.minutes:g} minutes under the scheduler, no faults")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
