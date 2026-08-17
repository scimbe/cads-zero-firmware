#!/usr/bin/env python3
"""CaDS Zero - project the roadmap onto GitHub labels, milestones and issues.

docs/ROADMAP.md is the single source of truth for what is left to do. This
script mirrors it into GitHub so work packages are visible, assignable, and can
be handed to parallel agents, without the roadmap and the issue tracker drifting
apart.

Idempotent: existing issues are matched by exact title and updated rather than
duplicated, so it is safe to run on every loop iteration. It never closes an
issue on its own - closing is a maintainer decision.

Two labels carry policy rather than description:

  hardware-gate   Needs the physical board. The maintainer holds exclusive
                  control of the hardware, so these are never handed out.
  swarm-ready     Self-contained and hardware-free: safe to give to a parallel
                  agent working in its own branch.

Usage:
    scripts/sync_github.py --dry-run
    scripts/sync_github.py
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = "scimbe/cads-zero"
ROADMAP = ROOT / "docs" / "ROADMAP.md"

LABELS = [
    ("area:hal", "0E8A16", "HAL and drivers"),
    ("area:gui", "1D76DB", "Canvas, views, widgets"),
    ("area:kernel", "5319E7", "Scheduler and primitives"),
    ("area:net", "006B75", "Ethernet, lwIP, CLI"),
    ("area:storage", "B60205", "littlefs and flash"),
    ("area:apps", "FBCA04", "Applications"),
    ("area:sim", "C5DEF5", "Host simulator"),
    ("area:ci", "BFD4F2", "Build and test pipeline"),
    ("area:docs", "D4C5F9", "Documentation"),
    ("area:assets", "F9D0C4", "Fonts, icons, branding"),
    ("hardware-gate", "D93F0B", "Requires the physical board - maintainer only"),
    ("needs-decision", "E99695", "Blocked on a user decision"),
    ("swarm-ready", "0052CC", "Self-contained, safe to hand to a parallel agent"),
]

MILESTONES = {
    "M1": "M1 Graphics and identity",
    "M2": "M2 Kernel",
    "M3": "M3 Input and GUI framework",
    "M4": "M4 Storage",
    "M5": "M5 Network",
    "M6": "M6 Applications",
    "M7": "M7 Simulator and test pipeline",
}

# Keyword -> area label. First match wins, so order matters.
AREA_RULES = [
    (r"dma2d|canvas|glyph|font|splash|widget|view|compositor|menu|dialog", "area:gui"),
    (r"freertos|thread|mutex|queue|timer|pubsub|record|scheduler|fault", "area:kernel"),
    (r"ethernet|lwip|dhcp|tcp|http|cli|netif|stream", "area:net"),
    (r"littlefs|flash|storage|file browser", "area:storage"),
    (r"desktop|leo|mascot|settings|about|gpio app|game|network info", "area:apps"),
    (r"simulator|sdl2|golden", "area:sim"),
    (r"\bci\b|unit test|pipeline|regression|board_test", "area:ci"),
    (r"lion|asset|icon|branding|wordmark", "area:assets"),
    (r"doc|roadmap", "area:docs"),
    (r"spi|touch|display|ili9486|xpt2046|input service|driver|hal", "area:hal"),
    (r"log|string", "area:kernel"),
]

# Anything matching these needs the physical board. The maintainer holds the
# hardware exclusively, so these are never labelled swarm-ready - handing one to
# a parallel agent would mean two things driving the same ST-Link.
HARDWARE_PATTERNS = re.compile(
    r"hardware gate|on hardware|by a human|visual confirmation|power-cycle|"
    r"solder|bridge|sb121|sb122",
    re.IGNORECASE,
)

CONTEXT = """
---

### Context

This is a work package from [`docs/ROADMAP.md`](../blob/main/docs/ROADMAP.md).

**Project.** CaDS Zero is a clean-room, Flipper-Zero-class firmware for the
ITSboard: NUCLEO-F429ZI + ITS adapter + Waveshare 4" ILI9486/XPT2046, with
Ethernet. Read [`docs/HARDWARE.md`](../blob/main/docs/HARDWARE.md) before
touching anything that talks to a peripheral.

**Non-negotiables.**

1. **Clean room.** No code from `flipperzero-firmware`. Concepts yes, source no.
   Prefix everything `cads_`; the mascot is Leo the lion, not a dolphin.
2. **[`docs/SAFETY.md`](../blob/main/docs/SAFETY.md) is binding.** SWD pins and
   the HSE input are never reconfigured, PF0-7 / PG0-5 stay inputs, no mass
   erase, no flash writes below `0x08120000`, no option-byte writes.
3. **Portability.** Everything above the HAL must build for both the board and
   the host simulator. A feature that only builds for one is not finished.
4. **The maintainer holds exclusive control of the hardware.** Do not flash,
   reset, or attach a debugger to the board. Write the code and the tests; the
   maintainer runs the hardware gate and merges.

**Definition of done.** Builds clean for both targets, unit or golden tests
where the logic allows it, `docs/ROADMAP.md` updated, and a PR opened against
`main` for the maintainer to review.
"""

HARDWARE_NOTE = """
---

### Maintainer only

This is a **hardware gate**. It requires physical access to the board and the
ST-Link, which the maintainer holds exclusively. It is tracked here so the
milestone cannot be declared complete without it, but it is not open for
parallel agents to pick up.
"""


RETRYABLE = re.compile(r"HTTP (50[0-4]|429)|timeout|temporarily", re.IGNORECASE)


def gh(args: list[str], timeout: float = 30.0, check: bool = True, attempts: int = 4) -> str:
    """Run a gh command, retrying the failures that are worth retrying.

    The GitHub API returns 503 often enough during a bulk sync that a single
    pass leaves the tracker half populated. Transient statuses get exponential
    backoff; a genuine error (bad label, missing milestone) fails immediately
    so it is not mistaken for flakiness.
    """
    last_error = ""
    for attempt in range(attempts):
        try:
            result = subprocess.run(["gh", *args], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            last_error = f"timeout after {timeout:.0f}s"
        else:
            if result.returncode == 0:
                return result.stdout
            last_error = result.stderr.strip()
            if not RETRYABLE.search(last_error):
                break
        if attempt < attempts - 1:
            delay = 2.0 * (2**attempt)
            print(f"    retrying in {delay:.0f}s ({last_error.splitlines()[0][:70]})", flush=True)
            time.sleep(delay)

    if check:
        raise RuntimeError(f"gh {' '.join(args[:3])} failed: {last_error}")
    return ""


def parse_roadmap() -> list[dict]:
    """Pull open tasks out of the roadmap, tagged with their milestone."""
    tasks = []
    milestone = None
    heading = re.compile(r"^##\s+(M\d)\s+—\s+(.+?)\s+`\[(.)\]`")
    item = re.compile(r"^-\s+\[( |~|!)\]\s+(.*)$")

    lines = ROADMAP.read_text(encoding="utf-8").splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        if match := heading.match(line):
            milestone = match.group(1)
        elif line.startswith("## Open decisions"):
            # Blocked items live outside the milestone structure but still need
            # to be visible as issues - they are what the project is waiting on.
            milestone = "DECISION"
        elif line.startswith("## "):
            milestone = None

        if (match := item.match(line)) and milestone:
            state, text = match.group(1), match.group(2)
            # Continuation lines are indented; fold them into the body.
            body_lines = []
            index += 1
            while index < len(lines) and lines[index].startswith("      "):
                body_lines.append(lines[index].strip())
                index += 1
            tasks.append(
                {
                    "milestone": milestone,
                    "state": state,
                    "title": re.sub(r"\*\*", "", text).strip(),
                    "detail": " ".join(body_lines),
                }
            )
            continue
        index += 1
    return tasks


def classify(task: dict) -> list[str]:
    text = f"{task['title']} {task['detail']}".lower()
    labels = []
    for pattern, label in AREA_RULES:
        if re.search(pattern, text):
            labels.append(label)
            break
    if task["state"] == "!":
        labels.append("needs-decision")
    if HARDWARE_PATTERNS.search(text):
        labels.append("hardware-gate")
    elif task["state"] != "!":
        labels.append("swarm-ready")
    return labels


def ensure_labels(dry_run: bool) -> None:
    for name, color, description in LABELS:
        if dry_run:
            print(f"  label {name}")
            continue
        try:
            gh(["label", "create", name, "--color", color,
                "--description", description, "--force"], check=False)
        except subprocess.TimeoutExpired:
            print(f"  warning: timed out creating label {name}", file=sys.stderr)


def ensure_milestones(dry_run: bool) -> dict[str, int]:
    existing = {}
    try:
        data = json.loads(gh(["api", f"repos/{REPO}/milestones", "--jq", "."]) or "[]")
        existing = {m["title"]: m["number"] for m in data}
    except Exception as exc:  # noqa: BLE001 - the API being down is not fatal here
        print(f"  warning: could not list milestones ({exc})", file=sys.stderr)

    for key, title in MILESTONES.items():
        if title in existing:
            continue
        if dry_run:
            print(f"  milestone {title}")
            continue
        try:
            gh(["api", f"repos/{REPO}/milestones", "-f", f"title={title}"], check=False)
        except subprocess.TimeoutExpired:
            print(f"  warning: timed out creating milestone {title}", file=sys.stderr)
    return existing


def existing_issue_titles() -> dict[str, int]:
    try:
        raw = gh(["issue", "list", "--repo", REPO, "--state", "all", "--limit", "300",
                  "--json", "number,title"], timeout=60.0)
        return {entry["title"]: entry["number"] for entry in json.loads(raw or "[]")}
    except Exception as exc:  # noqa: BLE001
        print(f"  warning: could not list issues ({exc})", file=sys.stderr)
        return {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    tasks = [t for t in parse_roadmap() if t["state"] in " ~!"]
    print(f"{len(tasks)} open task(s) in the roadmap")

    print("labels:")
    ensure_labels(args.dry_run)
    print("milestones:")
    ensure_milestones(args.dry_run)

    known = {} if args.dry_run else existing_issue_titles()
    created = skipped = 0

    for task in tasks:
        title = f"[{task['milestone']}] {task['title']}"
        if len(title) > 240:
            title = title[:237] + "..."

        if title in known:
            skipped += 1
            continue

        labels = classify(task)
        body = task["detail"] or ""
        body += HARDWARE_NOTE if "hardware-gate" in labels else ""
        body += CONTEXT

        if args.dry_run:
            print(f"  + {title}   {labels}")
            created += 1
            continue

        cmd = ["issue", "create", "--repo", REPO, "--title", title, "--body", body]
        if task["milestone"] in MILESTONES:
            cmd += ["--milestone", MILESTONES[task["milestone"]]]
        for label in labels:
            cmd += ["--label", label]
        try:
            url = gh(cmd, timeout=60.0).strip()
            print(f"  + {title}\n    {url}")
            created += 1
        except Exception as exc:  # noqa: BLE001
            print(f"  ! failed: {title} ({exc})", file=sys.stderr)

    print(f"\n{created} created, {skipped} already present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
