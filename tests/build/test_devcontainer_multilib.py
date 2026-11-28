"""The Dev Container can build 32-bit native_sim images on amd64 and still builds on arm64.

The default `native_sim` target compiles for the 32-bit ILP32 ABI, so it needs a host compiler
with multilib support; scripts/test-storage-simulators.sh runs `native_sim/native` as well as
`native_sim/native/64`. Zephyr's Ubuntu host package list includes gcc-multilib and
g++-multilib, and says they do not exist on AArch64. The image therefore installs them only when
the image's Debian architecture is amd64. The checks read the Dockerfile only; they do not build
the image.
"""
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DOCKERFILE = REPO / ".devcontainer" / "Dockerfile"
MULTILIB = ("gcc-multilib", "g++-multilib")


def steps():
    """Return the Dockerfile instructions in order, comments dropped, continuations joined."""
    result, current = [], ""
    for line in DOCKERFILE.read_text().splitlines():
        if not current and (not line.strip() or line.lstrip().startswith("#")):
            continue
        current += line.rstrip("\\").strip() + " "
        if not line.rstrip().endswith("\\"):
            result.append(current.strip())
            current = ""
    return result


def multilib_steps():
    return [s for s in steps() if s.startswith("RUN ") and any(p in s for p in MULTILIB)]


class DockerfileMultilibTest(unittest.TestCase):
    def test_one_step_installs_both_multilib_packages(self):
        found = multilib_steps()
        self.assertEqual(len(found), 1, found)
        for package in MULTILIB:
            self.assertRegex(found[0], rf"apt-get install [^;&|]*(?<![\w-]){re.escape(package)}\b")

    def test_the_step_runs_only_on_amd64(self):
        step = multilib_steps()[0]
        self.assertRegex(step, r'if \[ "\$\(dpkg --print-architecture\)" = amd64 \]; then')
        self.assertLess(step.index("dpkg --print-architecture"), step.index("apt-get install"))
        # The install sits between "then" and the closing "fi", so arm64 never runs it.
        self.assertRegex(step, r"; then .*apt-get install [^;]*gcc-multilib[^;]*;? fi$")
        self.assertLess(step.index("; then"), step.index("apt-get update"))
        self.assertEqual(step.count(" fi"), 1)

    def test_the_main_package_list_has_no_multilib_package(self):
        main = next(s for s in steps() if s.startswith("RUN apt-get"))
        for package in MULTILIB:
            self.assertNotIn(package, main)

    def test_the_step_cleans_the_apt_lists(self):
        self.assertIn("rm -rf /var/lib/apt/lists/*", multilib_steps()[0])

    def test_the_step_comes_after_the_organisation_ca(self):
        all_steps = steps()
        ca = next(i for i, s in enumerate(all_steps) if "install-org-ca.sh" in s and s.startswith("RUN "))
        self.assertGreater(all_steps.index(multilib_steps()[0]), ca)


if __name__ == "__main__":
    unittest.main()
