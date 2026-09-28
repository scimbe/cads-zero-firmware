#!/usr/bin/env python3
"""Self-test of scripts/integrate_solutions.sh against a throwaway firmware repo.

Standard library only (unittest), so CI needs nothing beyond python3, git,
bash and cmake/ctest. Run: python3 tests/scripts/test_integrate_solutions.py

The throwaway repo has the same interface as this one, minus the firmware:
scripts/build.sh writes build/itsboard/cads-zero.elf (a text file holding
RAM margin and flash size) and a linker map with one SRAM input section per
lesson; the three gate scripts print the lines the real ones print; a CMake
project with preset "host" has one ctest per lesson, label rnlab-LNN, green
once the solution ships solved/lNN.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "integrate_solutions.sh"

BUILD_SH = """#!/usr/bin/env bash
set -e
[ ! -f broken ] || { echo "error: broken"; exit 1; }
ram=0; flash=1000
mkdir -p build/itsboard
: > build/itsboard/cads-zero.map
for f in ram/*; do
    [ -f "$f" ] || continue
    n=$(cat "$f"); ram=$((ram + n)); nn=${f#ram/l}
    printf ' .bss.x%s   0x%x   0x%x apps/rnlab/libcads_app_rnlab.a(l%s_fake.c.obj)\\n' \\
        "$nn" $((0x20001000)) "$n" "$nn" >> build/itsboard/cads-zero.map
    # CCM (0x10000000) must not count towards a lesson's SRAM
    printf ' .ccm       0x%x   0x%x apps/rnlab/libcads_app_rnlab.a(l%s_fake.c.obj)\\n' \\
        $((0x10000000)) 4096 "$nn" >> build/itsboard/cads-zero.map
done
for f in solved/*; do [ -f "$f" ] && flash=$((flash + 100)); done
echo "$((50000 - ram)) $flash" > build/itsboard/cads-zero.elf
"""
RAM_PY = """import sys
margin = int(open(sys.argv[-1]).read().split()[0])
print(f"margin           = {margin} B")
print(f"PASS: {margin} B of margin, budget is 256 B")
"""
FLASH_PY = """import sys
flash = int(open(sys.argv[-1]).read().split()[1])
print(f"flash image (text+data) = {flash} B (x K)")
print(f"PASS: {524288 - flash} B of headroom under the 524288 B budget")
"""
FS_PY = 'print("PASS: no LOAD section occupies the littlefs window")\n'
CMAKE = """cmake_minimum_required(VERSION 3.20)
project(fake NONE)
enable_testing()
add_test(NAME test_base COMMAND ${CMAKE_COMMAND} -E true)
foreach(nn 01 02 03 04 05 06 07 08 09 10 11)
    add_test(NAME test_rnlab_l${nn} COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_SOURCE_DIR}/solved/l${nn})
    set_tests_properties(test_rnlab_l${nn} PROPERTIES LABELS rnlab-L${nn})
endforeach()
# L10 has optional deep-dive tests with their own label, green once the
# solution also ships solved/l10v.
add_test(NAME test_rnlab_l10_v COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_SOURCE_DIR}/solved/l10v)
set_tests_properties(test_rnlab_l10_v PROPERTIES LABELS rnlab-L10-vertiefung)
"""
PRESETS = """{
  "version": 3,
  "configurePresets": [
    {"name": "host", "generator": "Unix Makefiles", "binaryDir": "${sourceDir}/build/host"}
  ]
}
"""
README = """# rnlab

**Richtmaß je Lektion** (SRAM, statisch).

| Lektion | Richtmaß | Wofür |
|---|---:|---|
| L01 schichten | 1 KB | x |
| L03 subnetting | 0,5 KB | x |
"""


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, check=True, text=True,
                          capture_output=True).stdout.strip()


@unittest.skipIf(sys.platform == "win32" or not shutil.which("bash") or not shutil.which("cmake"),
                 "needs bash and cmake")
class IntegrateSolutionsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        tmp = Path(self._tmp.name)
        origin, fw = tmp / "origin.git", tmp / "fw"
        git(tmp, "init", "-q", "--bare", "-b", "praktikum/start", str(origin))
        (fw / "scripts").mkdir(parents=True)
        (fw / "apps" / "rnlab").mkdir(parents=True)
        files = {"scripts/build.sh": BUILD_SH, "scripts/check_ram_budget.py": RAM_PY,
                 "scripts/check_flash_budget.py": FLASH_PY, "scripts/check_fs_window.py": FS_PY,
                 "CMakeLists.txt": CMAKE, "CMakePresets.json": PRESETS,
                 "apps/rnlab/README.md": README,
                 "shared.txt": "line 1\nline 2\nline 3\n", ".gitignore": "build/\n"}
        for path, text in files.items():
            (fw / path).write_text(text)
        git(fw, "init", "-q", "-b", "praktikum/start")
        git(fw, "config", "user.name", "Test")
        git(fw, "config", "user.email", "test@example.org")
        self.commit(fw, "base")
        git(fw, "remote", "add", "origin", str(origin))
        git(fw, "push", "-q", "origin", "praktikum/start")
        self.fw, self.wt = fw, tmp / "cz-integ"

    def tearDown(self):
        self._tmp.cleanup()

    @staticmethod
    def commit(work, msg):
        git(work, "add", "-A")
        git(work, "commit", "-q", "-m", msg)

    def solution(self, nn, slug, ram=0, solved=True, extra=None):
        fw, name = self.fw, f"loesung/l{nn}-{slug}"
        git(fw, "checkout", "-q", "-b", name, "praktikum/start")
        (fw / "ram").mkdir(exist_ok=True)
        (fw / "ram" / f"l{nn}").write_text(str(ram))
        if solved:
            (fw / "solved").mkdir(exist_ok=True)
            (fw / "solved" / f"l{nn}").write_text("ok\n")
        for path, text in (extra or {}).items():
            (fw / path).write_text(text)
        self.commit(fw, f"solution L{nn}")
        git(fw, "push", "-q", "origin", name)
        git(fw, "checkout", "-q", "praktikum/start")

    def run_script(self, *args):
        env = {**os.environ, "INTEG_FW_REPO": str(self.fw), "INTEG_FW_BUILD_MAX": ""}
        env.pop("GITHUB_ACTIONS", None)
        return subprocess.run(["bash", str(SCRIPT), "--worktree", str(self.wt), *args],
                              cwd=self.fw, env=env, text=True, capture_output=True)

    @staticmethod
    def row(out, nn):
        return next(line for line in out.splitlines() if line.startswith(f"L{nn} "))

    def test_order_missing_and_deltas(self):
        self.solution("10", "http", ram=400, extra={"solved/l10v": "ok\n"})
        self.solution("02", "arp", ram=96)
        self.solution("01", "layers", ram=0)
        r = self.run_script()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        merged = [l.split()[-1] for l in r.stdout.splitlines() if ": merged " in l]
        self.assertEqual(merged, ["origin/loesung/l01-layers", "origin/loesung/l02-arp",
                                  "origin/loesung/l10-http"])
        self.assertIn("missing solutions: L03 L04 L05 L06 L07 L08 L09 L11", r.stdout)
        self.assertIn("96 B", self.row(r.stdout, "02"))
        self.assertIn("100 B", self.row(r.stdout, "02"))
        self.assertIn("RED (stub)", self.row(r.stdout, "05"))
        self.assertIn("integration: all green.", r.stdout)
        self.assertEqual(git(self.wt, "rev-parse", "--abbrev-ref", "HEAD"), "HEAD")  # detached
        self.assertNotIn("integ", git(self.fw, "ls-remote", "origin"))              # nothing pushed

    def test_conflict_between_two_lessons_names_both(self):
        self.solution("02", "arp", extra={"shared.txt": "line 1\nL02\nline 3\n"})
        self.solution("05", "dhcp", extra={"shared.txt": "line 1\nL05\nline 3\n"})
        r = self.run_script("--quick")
        self.assertEqual(r.returncode, 1)
        self.assertIn("CONFLICT merging origin/loesung/l05-dhcp", r.stdout)
        self.assertIn("shared.txt", r.stdout)
        self.assertIn("collides with: origin/loesung/l02-arp", r.stdout)

    def test_conflict_with_newer_base_asks_to_catch_up(self):
        self.solution("11", "app", extra={"shared.txt": "line 1\nL11\nline 3\n"})
        (self.fw / "shared.txt").write_text("line 1\nnew base\nline 3\n")
        self.commit(self.fw, "base moves on")
        git(self.fw, "push", "-q", "origin", "praktikum/start")
        self.solution("01", "layers", extra={"other.txt": "x\n"})
        r = self.run_script("--quick")
        self.assertEqual(r.returncode, 1)
        self.assertIn("praktikum/start itself", r.stdout)
        self.assertIn("1 commits behind", r.stdout)
        self.assertNotIn("l01-layers", r.stdout.split("CONFLICT")[1])

    def test_skip(self):
        self.solution("02", "arp", extra={"shared.txt": "line 1\nL02\nline 3\n"})
        self.solution("05", "dhcp", extra={"shared.txt": "line 1\nL05\nline 3\n"})
        r = self.run_script("--quick", "--skip", "05")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("(--skip)", self.row(r.stdout, "05"))

    def test_build_break_is_pinned_to_the_lesson(self):
        self.solution("01", "layers")
        self.solution("03", "subnetting", extra={"broken": "1\n"})
        r = self.run_script()
        self.assertEqual(r.returncode, 1)
        self.assertIn("BUILD BREAK only after merging origin/loesung/l03-subnetting", r.stdout)

    def test_red_lesson_test_fails_the_run(self):
        self.solution("04", "icmp", solved=False)
        r = self.run_script("--quick")
        self.assertEqual(r.returncode, 1)
        self.assertIn("RED", self.row(r.stdout, "04"))
        self.assertIn("integration: FAILED", r.stdout)

    def test_guideline_warning_counts_sram_not_ccm(self):
        self.solution("01", "layers", ram=512)     # guideline 1 KB -> fine
        self.solution("03", "subnetting", ram=2000)  # guideline 0,5 KB -> warning
        r = self.run_script()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)  # a warning, not a failure
        self.assertIn("512/1024 B", self.row(r.stdout, "01"))    # the 4 KB CCM entry did not count
        self.assertIn("WARNING L03 has 2000 B static SRAM, guideline 512 B", r.stdout)
        self.assertNotIn("WARNING L01", r.stdout)

    def test_vertiefung_label_is_run_and_reported_separately(self):
        self.solution("10", "http", extra={"solved/l10v": "ok\n"})
        self.solution("03", "subnetting")
        r = self.run_script("--quick")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        row10 = self.row(r.stdout, "10").split()
        self.assertEqual(row10[2:4], ["green", "green"])
        self.assertEqual(self.row(r.stdout, "03").split()[3], "-")   # L03 has no deep dive
        self.assertIn("RED (stub)", self.row(r.stdout, "05"))

    def test_red_vertiefung_of_a_merged_lesson_fails(self):
        self.solution("10", "http")   # required tests green, deep dive not solved
        r = self.run_script("--quick")
        self.assertEqual(r.returncode, 1)
        self.assertEqual(self.row(r.stdout, "10").split()[2:4], ["green", "RED"])
        self.assertIn("integration: FAILED", r.stdout)

    def test_rerun_reuses_the_worktree(self):
        self.solution("01", "layers")
        self.assertEqual(self.run_script("--quick").returncode, 0)
        r = self.run_script("--quick")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(len(git(self.wt, "log", "--merges", "--format=%s").splitlines()), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
