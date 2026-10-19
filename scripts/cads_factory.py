#!/usr/bin/env python3
"""CaDS Zero - read values from the central factory configuration.

config/factory.txt is the one source for the board's default configuration: the
firmware bakes it in (cmake/cads_factory_config.cmake), the unit tests read the
generated header, and host-side tools and board tests use this script so they
never carry their own copy of the board's IP address.

    scripts/cads_factory.py get net.ip           # 192.168.33.99
    scripts/cads_factory.py get net.gateway
    scripts/cads_factory.py rev                  # 8cf9aa93, the revision the firmware reports
    scripts/cads_factory.py dump                 # the normalised key = value lines

    BOARD_IP=$(scripts/cads_factory.py get net.ip)
    ping -c 3 "$BOARD_IP"

CADS_FACTORY_CONFIG (the same name the CMake option has) points at another file,
e.g. the one a lab image was built from. The grammar and the revision are
exactly CMake's: comments and blank lines are ignored, a line is
`section.key = value`, and the revision is the first 8 hex digits of the MD5 of
the normalised text.
"""

import argparse
import hashlib
import os
import re
import sys
from pathlib import Path

CADS_ROOT = Path(__file__).resolve().parent.parent
LINE_RE = re.compile(r"^([a-z0-9_]+\.[a-z0-9_]+)[ \t]*=[ \t]*(.*)$")


def factory_path():
    return Path(os.environ.get("CADS_FACTORY_CONFIG") or CADS_ROOT / "config" / "factory.txt")


BOOL_KEYS = {"boot.autostart", "display.fast_clock", "net.dhcp", "net.mac_random",
             "active.armed", "wifi.enabled"}
IP_KEYS = {"net.ip", "net.netmask", "net.gateway", "wifi.pcap_target"}
OCTET = r"(0|[1-9][0-9]?|1[0-9][0-9]|2[0-4][0-9]|25[0-5])"
IP_RE = re.compile(rf"^{OCTET}\.{OCTET}\.{OCTET}\.{OCTET}$")
BOOL_RE = re.compile(r"^(0|1|on|off|true|false|yes|no)$")


def check_value(path, lineno, key, value):
    """The same keys and value shapes cmake/cads_factory_config.cmake accepts: the
    firmware's parser silently skips an unknown key and falls back on a malformed
    value, which is fine for a hand-edited /config.txt and wrong for the file an
    image is built from. wifi.ssid / wifi.password are device-local and refused."""
    where = f"{path}:{lineno}: '{key} = {value}'"
    if key in BOOL_KEYS:
        ok, what = BOOL_RE.match(value), "a boolean"
    elif key in IP_KEYS:
        ok, what = IP_RE.match(value), "a dotted-decimal IPv4 address"
    elif key == "display.brightness":
        ok, what = re.match(r"^[0-9]+$", value), "a number"
    elif key == "wifi.uart":
        ok, what = re.match(r"^[a-z0-9_]+$", value), "a UART name"
    else:
        sys.exit(f"{where}: unknown key (wifi.ssid and wifi.password are device-local "
                 "and deliberately not part of the factory file)")
    if not ok:
        sys.exit(f"{where} is not {what}")


def parse(path):
    """Return the ordered [(key, value)] of a factory file, or exit on a bad line."""
    if not path.exists():
        sys.exit(f"{path}: no such file")
    pairs, seen = [], set()
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = LINE_RE.match(line)
        if not m:
            sys.exit(f"{path}:{lineno}: unrecognised line {raw!r} (expected 'section.key = value')")
        key, value = m.group(1), m.group(2).strip()
        if '"' in value or "\\" in value:
            sys.exit(f"{path}:{lineno}: value of {key} contains a quote or backslash")
        if key in seen:
            sys.exit(f"{path}:{lineno}: {key} is set twice")
        check_value(path, lineno, key, value)
        seen.add(key)
        pairs.append((key, value))
    return pairs


def normalised(pairs):
    return "".join(f"{k} = {v}\n" for k, v in pairs)


def revision(pairs):
    return hashlib.md5(normalised(pairs).encode()).hexdigest()[:8]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_get = sub.add_parser("get", help="print one value")
    p_get.add_argument("key")
    sub.add_parser("rev", help="print the factory revision")
    sub.add_parser("dump", help="print the normalised key = value lines")
    args = ap.parse_args(argv)

    pairs = parse(factory_path())
    if args.cmd == "get":
        values = dict(pairs)
        if args.key not in values:
            sys.exit(f"{args.key}: not set in {factory_path()} "
                     "(device-local keys such as wifi.ssid are deliberately absent)")
        print(values[args.key])
    elif args.cmd == "rev":
        print(revision(pairs))
    else:
        sys.stdout.write(normalised(pairs))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
