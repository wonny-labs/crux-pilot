#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The MMC cache flush correction refuses a drifted source and output inside Zephyr.

The good run needs the pinned Zephyr checkout, from ZEPHYR_BASE or beside the repository.
"""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
ZEPHYR = Path(os.environ.get("ZEPHYR_BASE", ROOT.parent / "zephyr"))
SPEC = importlib.util.spec_from_file_location("prepare_mmc_cache_flush",
                                              ROOT / "scripts/prepare-mmc-cache-flush.py")
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


def fake_zephyr(root, source):
    """A Zephyr tree holding only the given mmc.c bytes."""
    zephyr = root / "zephyr"
    (zephyr / PREPARE.RELATIVE_SOURCE).parent.mkdir(parents=True)
    (zephyr / PREPARE.RELATIVE_SOURCE).write_bytes(source)
    return zephyr


class Preparation(unittest.TestCase):
    def setUp(self):
        self.pinned = (ZEPHYR / PREPARE.RELATIVE_SOURCE).read_bytes()

    def test_source_with_one_changed_byte_is_rejected(self):
        changed = bytearray(self.pinned)
        changed[len(changed) // 2] ^= 0x01
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            zephyr = fake_zephyr(root, bytes(changed))
            output = root / "out/mmc.c"
            with self.assertRaises(ValueError):
                PREPARE.prepare(zephyr, output)
            self.assertFalse(output.exists())
            self.assertFalse(output.with_suffix(".json").exists())

    def test_output_inside_zephyr_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            zephyr = fake_zephyr(Path(directory), self.pinned)
            output = zephyr / "subsys/sd/generated/mmc.c"
            with self.assertRaises(ValueError):
                PREPARE.prepare(zephyr, output)
            self.assertFalse(output.exists())

    def test_good_run_writes_the_source_and_its_record(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            zephyr = fake_zephyr(root, self.pinned)
            output = root / "out/mmc.c"
            metadata = PREPARE.prepare(zephyr, output)
            generated = output.read_bytes()
            self.assertNotEqual(generated, self.pinned)
            self.assertIn(b"mmc_flush_cache", generated)
            self.assertEqual(json.loads(output.with_suffix(".json").read_text()), metadata)
            self.assertEqual(metadata["generated_sha256"], PREPARE.PATCHED_SHA256)
            self.assertEqual(metadata["upstream_sha256"], PREPARE.BASE_SHA256)
            self.assertEqual((zephyr / PREPARE.RELATIVE_SOURCE).read_bytes(), self.pinned)


if __name__ == "__main__":
    unittest.main()
