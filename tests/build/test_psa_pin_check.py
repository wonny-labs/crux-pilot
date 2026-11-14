"""Check the pin consistency step of scripts/test-psa-crypto.sh and the security pin exception of ADR-0006.

The real function text runs in bash against a fake repository, workspace and module checkouts. Only
`git rev-parse HEAD` is replaced, so that a checkout can sit at any revision.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts/test-psa-crypto.sh"
BASH = shutil.which("bash")
OLD_MBEDTLS = "a3e190fe44c78d1ba67f55979e1257328cc7d0d8"
OLD_TF = "dc575a2ddcc8cb16275d24c42a52eaf79ebe2231"
NEW_MBEDTLS = "098e120afb51877ed814bdc04443890ae12e0642"
NEW_TF = "fa92349590a54959ee2efc21b668e5afdb144726"
OTHER = "1" * 40
RECORD = "docs/dependency-review-mbed-tls-4.1.1.md"

DRIVER = r"""
set -uo pipefail
body=$(awk '/^pinned_revision\(\)/,/^# The result has the expected counts/' "$SCRIPT" | sed '$d')
git() { if [[ "$*" == *"rev-parse HEAD"* ]]; then case "$*" in *tf-psa-crypto*) echo "$CHECKOUT_TF";; *) echo "$CHECKOUT_MBEDTLS";; esac; else command git "$@"; fi; }
require_module() { :; }
eval "$body"
( set -e; fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }; check_pins )
"""


def manifest(mbedtls, tf):
    lines = ["manifest:", "  projects:"]
    for name, revision in (("mbedtls", mbedtls), ("tf-psa-crypto", tf)):
        lines += [f"    - name: {name}", f"      revision: {revision}"]
    return "\n".join(lines) + "\n"


@unittest.skipUnless(BASH, "bash is needed")
class PinCheckTest(unittest.TestCase):
    def run_check(self, west, zephyr, record=True, zephyr_manifest=True, cmake_pin=None):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "ws"
            repo = workspace / "crux-pilot"
            (repo / "cmake").mkdir(parents=True)
            (repo / "docs").mkdir()
            (workspace / "zephyr").mkdir()
            if record:
                (repo / RECORD).write_text("record\n")
            (repo / "west.yml").write_text(manifest(*west))
            if zephyr_manifest:
                (workspace / "zephyr/west.yml").write_text(manifest(*zephyr))
            (repo / "cmake/CruxPsaCrypto.cmake").write_text(f"set(CRUX_TF_PSA_CRYPTO_REVISION {cmake_pin or west[1]})\n")
            env = {"PATH": "/usr/bin:/bin", "SCRIPT": str(SCRIPT), "repo": str(repo), "workspace": str(workspace),
                   "modules": str(workspace / "modules"), "scope": "tf-psa-crypto",
                   "CHECKOUT_MBEDTLS": west[0], "CHECKOUT_TF": west[1]}
            return subprocess.run([BASH, "-c", DRIVER], env=env, capture_output=True, text=True, check=False)

    def test_equal_pins_pass(self):
        result = self.run_check((OLD_MBEDTLS, OLD_TF), (OLD_MBEDTLS, OLD_TF))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertNotIn("security pin", result.stdout)

    def test_the_reviewed_security_pins_pass_over_zephyrs_older_pins(self):
        result = self.run_check((NEW_MBEDTLS, NEW_TF), (OLD_MBEDTLS, OLD_TF))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn(f"security pin: mbedtls {NEW_MBEDTLS}", result.stdout)
        self.assertIn(f"security pin: tf-psa-crypto {NEW_TF}", result.stdout)
        self.assertIn(RECORD, result.stdout)

    def test_pins_equal_to_a_later_zephyr_pass_without_the_exception(self):
        result = self.run_check((NEW_MBEDTLS, NEW_TF), (NEW_MBEDTLS, NEW_TF))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertNotIn("security pin", result.stdout)

    def test_the_exception_needs_its_review_record(self):
        result = self.run_check((NEW_MBEDTLS, NEW_TF), (OLD_MBEDTLS, OLD_TF), record=False)
        self.assertEqual(1, result.returncode)
        self.assertIn("FAIL: west.yml pins mbedtls", result.stderr)

    def test_an_unlisted_revision_fails_for_each_module(self):
        for west in ((OTHER, NEW_TF), (NEW_MBEDTLS, OTHER)):
            with self.subTest(west=west):
                result = self.run_check(west, (OLD_MBEDTLS, OLD_TF))
                self.assertEqual(1, result.returncode)
                self.assertIn("FAIL: west.yml pins", result.stderr)

    def test_the_exception_lapses_when_zephyrs_pin_moves_to_another_revision(self):
        result = self.run_check((NEW_MBEDTLS, NEW_TF), (OTHER, OLD_TF))
        self.assertEqual(1, result.returncode)
        self.assertIn(f"Zephyr's manifest at {OTHER}", result.stderr)

    def test_an_unreadable_zephyr_manifest_does_not_pass(self):
        result = self.run_check((NEW_MBEDTLS, NEW_TF), (OLD_MBEDTLS, OLD_TF), zephyr_manifest=False)
        self.assertEqual(1, result.returncode)
        self.assertIn("Zephyr's manifest at nothing", result.stderr)

    def test_cmake_and_west_must_still_agree(self):
        result = self.run_check((NEW_MBEDTLS, NEW_TF), (OLD_MBEDTLS, OLD_TF), cmake_pin=OLD_TF)
        self.assertEqual(1, result.returncode)
        self.assertIn("cmake/CruxPsaCrypto.cmake pins", result.stderr)


if __name__ == "__main__":
    unittest.main()
