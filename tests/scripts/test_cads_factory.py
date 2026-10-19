"""scripts/cads_factory.py: the host-side reader of config/factory.txt.

The point of the script is that tools and board tests agree with the firmware,
so the main check compares it with what CMake generated from the same file
(revision and values). That check needs a configured build; it skips when none
exists. Run: python3 -m unittest tests/scripts/test_cads_factory.py
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "cads_factory.py"
HEADER = Path(os.environ.get(
    "CADS_FACTORY_HEADER", ROOT / "build" / "host" / "generated" / "cads" / "config" / "cads_factory_config.h"))


def run(*args, factory=None):
    env = dict(os.environ)
    env.pop("CADS_FACTORY_CONFIG", None)
    if factory is not None:
        env["CADS_FACTORY_CONFIG"] = str(factory)
    return subprocess.run([sys.executable, str(SCRIPT), *args], capture_output=True, text=True, env=env)


class FactoryScript(unittest.TestCase):
    def write(self, text):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        p = Path(d.name) / "factory.txt"
        p.write_text(text)
        return p

    def test_get_returns_the_value(self):
        p = self.write("# c\nnet.ip = 10.1.2.3   # trailing comment\nnet.dhcp=0\n")
        self.assertEqual(run("get", "net.ip", factory=p).stdout, "10.1.2.3\n")
        self.assertEqual(run("get", "net.dhcp", factory=p).stdout, "0\n")

    def test_missing_key_is_an_error(self):
        p = self.write("net.ip = 10.1.2.3\n")
        r = run("get", "wifi.ssid", factory=p)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not set", r.stderr)

    def test_bad_input_is_rejected_like_cmake_does(self):
        for text in ("net.ip = 1\nnet.ip = 2\n", "just words\n", 'net.ip = "x"\n', "net.ip = a\\b\n"):
            r = run("dump", factory=self.write(text))
            self.assertNotEqual(r.returncode, 0, text)

    def test_revision_ignores_comments_and_whitespace_but_not_values(self):
        a = run("rev", factory=self.write("net.ip = 10.0.0.1\nnet.dhcp = 0\n")).stdout
        b = run("rev", factory=self.write("# hi\n\nnet.ip=10.0.0.1  \n  net.dhcp =   0\n")).stdout
        c = run("rev", factory=self.write("net.ip = 10.0.0.2\nnet.dhcp = 0\n")).stdout
        self.assertEqual(a, b)
        self.assertNotEqual(a, c)

    @unittest.skipUnless(HEADER.exists(), "no configured host build (build/host) to compare against")
    def test_agrees_with_the_header_cmake_generated(self):
        header = HEADER.read_text()
        cmake_rev = re.search(r"CADS_FACTORY_CONFIG_REV 0x([0-9a-f]{8})u", header).group(1)
        self.assertEqual(run("rev").stdout.strip(), cmake_rev)
        for key, macro in (("net.ip", "NET_IP"), ("net.netmask", "NET_NETMASK"), ("net.gateway", "NET_GATEWAY")):
            m = re.search(rf'#define CADS_FACTORY_{macro} "([^"]*)"', header)
            self.assertEqual(run("get", key).stdout.strip(), m.group(1), key)


if __name__ == "__main__":
    unittest.main()
