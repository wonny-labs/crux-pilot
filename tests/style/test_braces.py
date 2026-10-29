"""Exercise the pinned Zephyr BRACES check through Crux's diff-based wrapper."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/check-braces.sh"


class BracesGateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        zephyr = os.environ.get("ZEPHYR_BASE")
        if not zephyr or not (Path(zephyr) / "scripts/checkpatch.pl").is_file():
            raise AssertionError("Tests require the prepared pinned ZEPHYR_BASE")

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="crux-braces-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Crux test")
        self.git("config", "user.email", "crux-test@example.invalid")

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.root, check=True,
                       capture_output=True, text=True)

    def write(self, path, content):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    def commit(self, message):
        self.git("add", "--all")
        self.git("commit", "-q", "-m", message)

    def check(self, env=None):
        return subprocess.run(["bash", str(SCRIPT), "HEAD^", "HEAD"],
                              cwd=self.root, capture_output=True, text=True,
                              env=env or os.environ.copy())

    def test_rejects_new_unbraced_if(self):
        self.write("component.c", "int base(void)\n{\n    return 0;\n}\n")
        self.commit("baseline")
        self.write("component.c", "int base(void)\n{\n    return 0;\n}\n"
                   "\nint changed(int value)\n{\n    if (value)\n"
                   "        return 1;\n    return 0;\n}\n")
        self.commit("missing braces")
        result = self.check()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("braces {} are required", result.stdout + result.stderr)

    def test_accepts_new_braced_if(self):
        self.write("component.c", "int base(void)\n{\n    return 0;\n}\n")
        self.commit("baseline")
        self.write("component.c", "int base(void)\n{\n    return 0;\n}\n"
                   "\nint changed(int value)\n{\n    if (value) {\n"
                   "        return 1;\n    }\n    return 0;\n}\n")
        self.commit("braced condition")
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_untouched_legacy_if_does_not_fail(self):
        source = ("int legacy(int value)\n{\n    if (value)\n"
                  "        return 1;\n    return 0;\n}\n")
        self.write("component.c", source)
        self.commit("baseline")
        self.write("component.c", source.replace("    return 0;\n",
                   "    /* Unrelated annotation. */\n    return 0;\n"))
        self.commit("comment change")
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_non_c_change_does_not_trigger_check(self):
        self.write("component.c", "int legacy(int x)\n{\n"
                   "    if (x)\n        return 1;\n    return 0;\n}\n")
        self.commit("baseline")
        self.write("notes.md", "Documentation change.\n")
        self.commit("documentation only")
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("No changed C source", result.stdout)

    def test_missing_checker_is_not_silently_skipped(self):
        self.write("component.c", "int base(void)\n{\n    return 0;\n}\n")
        self.commit("baseline")
        self.write("component.c", "int base(void)\n{\n    return 1;\n}\n")
        self.commit("change")
        env = os.environ.copy()
        env["ZEPHYR_BASE"] = str(self.root / "missing-zephyr")
        result = self.check(env)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Set ZEPHYR_BASE", result.stderr)


if __name__ == "__main__":
    unittest.main()
