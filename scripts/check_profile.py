#!/usr/bin/env python3
"""CaDS Zero - validate a build profile (profiles/*.profile) before spending a
build cycle on it.

Two checks, in order:

  1. Syntax/known-keys - the same rules CMakeLists.txt applies when it reads
     CADS_PROFILE (`app.<name> = on|off`, only the apps this project actually
     has), so a typo is caught here in milliseconds instead of as a CMake
     FATAL_ERROR after a slower configure.
  2. View-registry capacity - apps/bringup/explorer_app_demo.c's
     CADS_APP_DEMO_VIEW_CAPACITY is a fixed array size that must be big
     enough for however many views the ENABLED apps register. Getting this
     wrong does not fail a build - cads_view_dispatcher_add() returns false
     and every caller discards it with `(void)`, so the overflow views
     silently never exist (this exact bug shipped twice this project's own
     history - see docs/ROADMAP.md's 2026-08 log). This script computes the
     view count a profile would need from a fixed per-app table and compares
     it against the constant read straight out of the source, so a profile
     that would silently drop a view fails HERE instead of on a real board.

`--build` additionally does a real configure+build in a scratch directory and
runs check_ram_budget.py against the result - the full-usability check (does
this profile actually fit the 48K RAM floor), not just static analysis.
Slower (a real ARM toolchain build), off by default.

Usage:
    scripts/check_profile.py profiles/full.profile
    scripts/check_profile.py profiles/minimal.profile --build
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

CADS_ROOT = Path(__file__).resolve().parent.parent

# Kept in sync BY HAND with CMakeLists.txt's CADS_PROFILE_KNOWN_APPS and
# apps/bringup/explorer_app_demo.c's actual cads_view_dispatcher_add() call
# sites (desktop + menu are unconditional, not profile-controlled apps, so
# they are added separately below rather than listed here). A CI job
# (test_app_tree's own capacity assertion, host-side) is the real backstop
# if this table ever drifts from the source; this script is the fast,
# pre-build check, not the last line of defense.
KNOWN_APPS = {
    "settings": 4,      # main + confirm + touch-calib + test-pattern
    "about": 1,
    "gpio": 1,
    "netinfo": 1,
    "filebrowser": 2,
    "game": 5,
    "netiperf": 2,
    "nettools": 4,
    "active": 2,
}
UNCONDITIONAL_VIEWS = 2  # desktop + the menu itself


def parse_profile(path):
    """Return {app_name: bool}. Raises SystemExit with a clear message on
    any syntax/unknown-key problem, matching CMakeLists.txt's own rules."""
    values = {}
    line_re = re.compile(r"^app\.([a-z_]+)\s*=\s*(on|off|1|0)$", re.IGNORECASE)
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = line_re.match(line)
        if not m:
            sys.exit(f"{path}:{lineno}: unrecognised line {raw!r} "
                     f"(expected 'app.<name> = on|off')")
        name, val = m.group(1).lower(), m.group(2).lower()
        if name not in KNOWN_APPS:
            sys.exit(f"{path}:{lineno}: unknown app '{name}' - known: "
                     f"{', '.join(sorted(KNOWN_APPS))}")
        if name in values:
            sys.exit(f"{path}:{lineno}: '{name}' set twice in this profile")
        values[name] = val in ("on", "1")
    return values


def read_capacity():
    src = CADS_ROOT / "apps/bringup/explorer_app_demo.c"
    m = re.search(r"#define CADS_APP_DEMO_VIEW_CAPACITY (\d+)u", src.read_text())
    if not m:
        sys.exit(f"could not find CADS_APP_DEMO_VIEW_CAPACITY in {src}")
    return int(m.group(1))


def check_capacity(enabled):
    capacity = read_capacity()
    needed = UNCONDITIONAL_VIEWS + sum(KNOWN_APPS[a] for a in enabled if enabled[a])
    print(f"view registry: {needed} view(s) needed for this profile, "
          f"capacity is {capacity}")
    if needed > capacity:
        print(f"FAIL: this profile needs {needed} views but "
              f"CADS_APP_DEMO_VIEW_CAPACITY is only {capacity} - "
              f"the excess would be silently dropped at registration "
              f"(cads_view_dispatcher_add returns false, discarded). "
              f"Raise the constant in apps/bringup/explorer_app_demo.c "
              f"(and its mirror in tests/unit/test_app_tree.c) before "
              f"building this profile.")
        return False
    print(f"PASS: {capacity - needed} view slot(s) to spare")
    return True


def check_ram(profile_path):
    build_dir = CADS_ROOT / "build" / "profile-check"
    print(f"\nconfiguring + building {profile_path.name} into {build_dir} "
          f"(this needs the ARM toolchain, may take a minute)...")
    env_and_cmake = (
        f'source "{CADS_ROOT}/scripts/cads_env.sh" && '
        f'cmake -S "{CADS_ROOT}" -B "{build_dir}" -G Ninja '
        f'-DCMAKE_TOOLCHAIN_FILE="{CADS_ROOT}/cmake/arm-none-eabi-gcc.cmake" '
        f'-DCMAKE_BUILD_TYPE=Release '
        f'-DCADS_PROFILE="{profile_path}" && '
        f'cmake --build "{build_dir}"'
    )
    rc = subprocess.run(["bash", "-c", env_and_cmake]).returncode
    if rc != 0:
        print("FAIL: configure/build failed - see the output above")
        return False

    elf = build_dir / "cads-zero.elf"
    print(f"\nchecking RAM budget for {elf}...")
    rc = subprocess.run(
        [sys.executable, str(CADS_ROOT / "scripts/check_ram_budget.py"), str(elf)]
    ).returncode
    return rc == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("profile", type=Path, help="path to a profiles/*.profile file")
    ap.add_argument("--build", action="store_true",
                     help="also configure+build the profile and check its RAM budget")
    args = ap.parse_args()

    if not args.profile.exists():
        sys.exit(f"no such file: {args.profile}")

    enabled = parse_profile(args.profile)
    print(f"{args.profile}: {sum(enabled.values())}/{len(KNOWN_APPS)} apps on "
          f"({', '.join(sorted(a for a, v in enabled.items() if v)) or 'none'})")

    ok = check_capacity(enabled)
    if args.build:
        ok = check_ram(args.profile) and ok

    if not ok:
        sys.exit(1)
    print("\nprofile OK" + (" (capacity only - pass --build for a full RAM check)" if not args.build else ""))


if __name__ == "__main__":
    main()
