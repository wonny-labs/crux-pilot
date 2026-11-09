#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Row C11: the patch of the TCP initial sequence number applies cleanly to the pinned Zephyr, and the script that
applies it checks every hash. Host only, no Zephyr build.

It needs the pinned Zephyr checkout, named by ZEPHYR_BASE (read-only: the test only reads tcp.c from it), and git.
"""
import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
if not os.environ.get("ZEPHYR_BASE"):
    raise RuntimeError("ZEPHYR_BASE is unset: set it to the pinned Zephyr checkout to run test_prepare.py")
ZEPHYR = Path(os.environ["ZEPHYR_BASE"])
SCRIPT = ROOT / "scripts/prepare-isn-fail-closed.py"
PATCH = ROOT / "patches/zephyr/zephyr-isn-fail-closed.patch"
DECLARATION = ROOT / "tests/zephyr/management_rt1060_compile/bench_declaration.json"
SOURCE = Path("subsys/net/ip/tcp.c")


def load():
    spec = importlib.util.spec_from_file_location("prepare_isn_fail_closed", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(data):
    return hashlib.sha256(data).hexdigest()


def west_pin():
    lines = (ROOT / "west.yml").read_text().splitlines()
    inside = False
    for line in lines:
        words = line.split()
        if words[:2] == ["-", "name:"]:
            inside = words[2:3] == ["zephyr"]
        if inside and words[:1] == ["revision:"]:
            return words[1]
    raise AssertionError("no zephyr pin in west.yml")


class Patch(unittest.TestCase):
    def setUp(self):
        self.prepare = load()
        self.pinned = (ZEPHYR / SOURCE).read_bytes()

    def copy(self, directory, source=None):
        tree = Path(directory) / "zephyr-copy"
        (tree / SOURCE).parent.mkdir(parents=True)
        (tree / SOURCE).write_bytes(self.pinned if source is None else source)
        return tree

    def test_the_pinned_file_is_the_one_the_patch_is_for(self):
        self.assertEqual(sha(self.pinned), self.prepare.BASE_SHA256)

    def test_the_patch_applies_to_the_pinned_file_and_gives_the_declared_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / SOURCE).parent.mkdir(parents=True)
            (work / SOURCE).write_bytes(self.pinned)
            subprocess.run(["git", "apply", "--no-index", "--check", "--whitespace=error-all", str(PATCH)], cwd=work, check=True)
            subprocess.run(["git", "apply", "--no-index", "--whitespace=error-all", str(PATCH)], cwd=work, check=True)
            self.assertEqual(sha((work / SOURCE).read_bytes()), self.prepare.PATCHED_SHA256)

    def test_the_declaration_names_this_patch_and_these_hashes(self):
        declared = json.loads(DECLARATION.read_text())["patched_zephyr"]["isn_fail_closed"]
        self.assertEqual(declared["patch_name"], PATCH.name)
        self.assertEqual(declared["patch_sha256"], sha(PATCH.read_bytes()))
        self.assertEqual(declared["patch_sha256"], self.prepare.PATCH_SHA256)
        self.assertEqual(declared["patched_file"], str(SOURCE))
        self.assertEqual(declared["original_file_sha256"], self.prepare.BASE_SHA256)
        self.assertEqual(declared["patched_file_sha256"], self.prepare.PATCHED_SHA256)
        self.assertEqual(declared["marker_file"], self.prepare.MARKER)

    def test_prepare_patches_a_copy_in_place_and_writes_the_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            self.prepare.prepare(tree, west_pin())
            self.assertEqual(sha((tree / SOURCE).read_bytes()), self.prepare.PATCHED_SHA256)
            self.assertEqual((tree / self.prepare.MARKER).read_text().strip(), f"{west_pin()} {self.prepare.PATCH_SHA256}")

    def test_prepare_twice_changes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            self.prepare.prepare(tree, west_pin())
            once = (tree / SOURCE).read_bytes()
            self.prepare.prepare(tree, west_pin())
            self.assertEqual((tree / SOURCE).read_bytes(), once)

    def test_a_source_with_one_changed_byte_is_refused_and_left_alone(self):
        changed = bytearray(self.pinned)
        changed[len(changed) // 2] ^= 0x01
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory, bytes(changed))
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())
            self.assertEqual((tree / SOURCE).read_bytes(), bytes(changed))
            self.assertFalse((tree / self.prepare.MARKER).exists())

    def test_a_patched_file_without_the_marker_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            self.prepare.prepare(tree, west_pin())
            (tree / self.prepare.MARKER).unlink()
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())

    def test_a_checkout_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            (tree / ".git").mkdir()
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())
            self.assertEqual((tree / SOURCE).read_bytes(), self.pinned)

    def test_a_drifted_patch_file_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            drifted = Path(directory) / "drifted.patch"
            drifted.write_bytes(PATCH.read_bytes() + b"\n")
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin(), patch=drifted)
            self.assertEqual((tree / SOURCE).read_bytes(), self.pinned)

    def test_the_patch_leaves_the_key_check_and_the_hash_size_in_the_file(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            self.prepare.prepare(tree, west_pin())
            text = (tree / SOURCE).read_text()
            self.assertIn("ret = sys_csrand_get(key, sizeof(key));", text)
            self.assertIn("uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];", text)
            self.assertNotIn("uint8_t hash[16];", text)
            # no call to the key read is left that ignores the result, or that reads into the key in use
            self.assertEqual(text.count("sys_csrand_get(key"), 1)
            self.assertNotIn("sys_csrand_get(unique_key", text)


if __name__ == "__main__":
    unittest.main()
