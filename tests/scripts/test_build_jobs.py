"""scripts/cads_env.sh cads_build_jobs: parallel jobs for `cmake --build`.

The job count comes from the container's CPU quota (cgroup v2 cpu.max, cgroup v1
as fallback), not from nproc, which reports the host's cores in a container.
Without any cgroup CPU file (macOS, Windows, a plain Linux host) it prints
nothing, so scripts/build.sh passes no -j and behaves as before.
Run: python3 -m unittest tests/scripts/test_build_jobs.py
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
ENV_SH = ROOT / "scripts" / "cads_env.sh"


class BuildJobs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="cads-jobs-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        # PATH without any cads-jobs of an image the tests might run in.
        self.path = os.pathsep.join(p for p in os.environ.get("PATH", "").split(os.pathsep)
                                    if p and not os.path.exists(os.path.join(p, "cads-jobs")))

    def jobs(self, v2=None, quota=None, period=None, nproc="40", extra_path=None):
        def place(name, content):
            f = os.path.join(self.tmp, name)
            if content is not None:
                Path(f).write_text(content)
            return f
        env = {
            "PATH": (extra_path + os.pathsep if extra_path else "") + self.path,
            "HOME": self.tmp,
            "CADS_JOBS_CPU_MAX": place("cpu.max", v2),
            "CADS_JOBS_V1_QUOTA": place("quota", quota),
            "CADS_JOBS_V1_PERIOD": place("period", period),
            "CADS_JOBS_NPROC": nproc,
        }
        r = subprocess.run(["bash", "-c", f'source "{ENV_SH}"; cads_build_jobs'],
                           capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout

    def test_examples_cgroup_v2(self):
        for content, want in [("300000 100000\n", "3\n"), ("150000 100000\n", "2\n"),
                              ("50000 100000\n", "1\n"), ("100000 100000\n", "1\n")]:
            with self.subTest(content=content):
                self.assertEqual(self.jobs(v2=content), want)

    def test_no_limit_or_broken_value_gives_the_cpu_count(self):
        for content in ["max 100000\n", "", "kaputt\n", "-1 100000\n", "100000 0\n", "100000\n", "1 2 3\n", "* *\n"]:
            with self.subTest(content=content):
                self.assertEqual(self.jobs(v2=content), "40\n")

    def test_never_more_than_the_cpu_count(self):
        self.assertEqual(self.jobs(v2="800000 100000\n", nproc="4"), "4\n")

    def test_no_cgroup_file_prints_nothing_so_build_sh_passes_no_j(self):
        self.assertEqual(self.jobs(), "")

    def test_cgroup_v1_fallback(self):
        self.assertEqual(self.jobs(quota="250000\n", period="100000\n"), "3\n")
        self.assertEqual(self.jobs(quota="-1\n", period="100000\n"), "40\n")

    def test_cgroup_v2_wins_over_v1(self):
        self.assertEqual(self.jobs(v2="200000 100000\n", quota="600000\n", period="100000\n"), "2\n")

    def test_uses_cads_jobs_of_the_lab_image_when_present(self):
        fake = os.path.join(self.tmp, "bin")
        os.mkdir(fake)
        Path(fake, "cads-jobs").write_text("#!/bin/sh\necho 7\n")
        os.chmod(os.path.join(fake, "cads-jobs"), 0o755)
        self.assertEqual(self.jobs(v2="300000 100000\n", extra_path=fake), "7\n")

    def test_build_sh_passes_j_only_when_there_is_a_number(self):
        text = (ROOT / "scripts" / "build.sh").read_text()
        self.assertIn('JOBS="$(cads_build_jobs)"', text)
        self.assertIn('cmake --build "${BUILD_DIR}" ${JOBS:+-j "${JOBS}"}', text)


if __name__ == "__main__":
    unittest.main()
