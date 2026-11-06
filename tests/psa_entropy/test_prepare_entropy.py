#!/usr/bin/env python3
"""Check the generated Mbed TLS entropy source against the pinned Zephyr source."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ZEPHYR = Path(os.environ.get("ZEPHYR_BASE", ROOT.parent / "zephyr"))
SPEC = importlib.util.spec_from_file_location("prepare_entropy", ROOT / "scripts/prepare-mbedtls-entropy.py")
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)
CALL = "get_random_data(output, output_size, "


class GeneratedEntropySourceTests(unittest.TestCase):
    def test_only_the_fallback_flag_changes(self):
        original = (ZEPHYR / PREPARE.RELATIVE_SOURCE).read_text()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "zephyr_entropy.c"
            metadata = PREPARE.prepare(ZEPHYR, output)
            generated = output.read_text()
        self.assertEqual(metadata["generated_sha256"], PREPARE.PATCHED_SHA256)
        self.assertEqual(original.count(CALL + "true)"), 1)
        self.assertEqual(generated.count(CALL + "false)"), 1)
        self.assertNotIn(CALL + "true)", generated)
        # The external-RNG route keeps its Kconfig-controlled flag, which refuses the sys_rand_get() fallback.
        self.assertIn("IS_ENABLED(CONFIG_MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG_ALLOW_NON_CSPRNG)", generated)
        removed = [line for line in original.splitlines() if line not in generated.splitlines()]
        self.assertEqual(removed, ["\tif (get_random_data(output, output_size, true) < 0) {"])

    def test_a_drifted_upstream_source_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            drifted = work / "zephyr" / PREPARE.RELATIVE_SOURCE
            drifted.parent.mkdir(parents=True)
            drifted.write_text((ZEPHYR / PREPARE.RELATIVE_SOURCE).read_text() + "/* drift */\n")
            output = work / "generated" / "zephyr_entropy.c"
            with self.assertRaisesRegex(ValueError, "Unsupported upstream"):
                PREPARE.prepare(work / "zephyr", output)
            self.assertFalse(output.exists())

    def test_output_inside_the_zephyr_checkout_is_refused(self):
        with self.assertRaisesRegex(ValueError, "outside the Zephyr checkout"):
            PREPARE.prepare(ZEPHYR, ZEPHYR / "build" / "zephyr_entropy.c")


if __name__ == "__main__":
    unittest.main()
