#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Row C11: the patch of the TCP initial sequence number applies cleanly to the pinned Zephyr, and the script that
applies it checks every hash. Host only, no Zephyr build.

T11 (docs/rt1060-bench-freeze-and-capture-design.md): the copy of the bench image has ONE marker, `.crux-wolf-tls`, which
the TLS step writes as `<pin> <tls sha>` and this step extends to `<pin> <tls sha> <isn sha>`. It writes no marker of its own.
A copy without the TLS marker is the standalone mode of scripts/test-isn-fail-closed.sh: the patch is applied and no marker
is written, so the bench guard refuses such a copy.

It needs the pinned Zephyr checkout, named by ZEPHYR_BASE (read-only: the test only reads tcp.c from it), and git.
"""
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
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
TLS_MARKER = ".crux-wolf-tls"
LEGACY_MARKER = ".crux-isn-fail-closed"
sys.path.insert(0, str(ROOT / "tests/target"))
import zephyr_tree  # noqa: E402  (the marker text of the freeze: the one definition both sides use)


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

    def tls_copy(self, directory, text=None, source=None):
        """A copy as scripts/prepare-wolf-tls-zephyr.sh leaves it as far as this step sees it: the marker `<pin> <tls sha>`."""
        tree = self.copy(directory, source)
        (tree / TLS_MARKER).write_text(f"{west_pin()} {self.prepare.TLS_PATCH_SHA256}\n" if text is None else text)
        return tree

    def snapshot(self, tree):
        return {p.relative_to(tree).as_posix(): p.read_bytes() for p in sorted(tree.rglob("*")) if p.is_file()}

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
        # Schema 2 of the declaration (T11): the patch is the second entry of the ordered list, its file one entry of patched_files.
        block = json.loads(DECLARATION.read_text())["patched_zephyr"]
        patch = block["patches"][1]
        self.assertEqual(patch["name"], PATCH.name)
        self.assertEqual(patch["sha256"], sha(PATCH.read_bytes()))
        self.assertEqual(patch["sha256"], self.prepare.PATCH_SHA256)
        self.assertEqual(block["patches"][0]["sha256"], self.prepare.TLS_PATCH_SHA256)
        entry = next(f for f in block["patched_files"] if f["path"] == str(SOURCE))
        self.assertEqual(entry["patches"], [PATCH.name])
        self.assertEqual(entry["sha256"], self.prepare.PATCHED_SHA256)
        self.assertEqual(block["stock_sha256"][str(SOURCE)], self.prepare.BASE_SHA256)
        self.assertEqual(block["marker_file"], TLS_MARKER)

    def test_the_two_scripts_agree_on_the_tls_patch_and_the_marker(self):
        script = (ROOT / "scripts/prepare-wolf-tls-zephyr.sh").read_text()
        self.assertIn(f"patch_sha256={self.prepare.TLS_PATCH_SHA256}", script)
        self.assertIn(TLS_MARKER, script)
        self.assertEqual(self.prepare.TLS_MARKER, TLS_MARKER)
        self.assertEqual(self.prepare.LEGACY_MARKER, LEGACY_MARKER)

    def test_prepare_patches_a_copy_in_place_and_extends_the_one_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory)
            self.assertEqual(self.prepare.prepare(tree, west_pin()), "prepared")
            self.assertEqual(sha((tree / SOURCE).read_bytes()), self.prepare.PATCHED_SHA256)
            # byte for byte the text of the freeze: `<pin> <hash 1> <hash 2>` and one newline, the TLS patch first
            self.assertEqual((tree / TLS_MARKER).read_text(),
                             zephyr_tree.marker_text(west_pin(), [self.prepare.TLS_PATCH_SHA256, self.prepare.PATCH_SHA256]))
            self.assertFalse((tree / LEGACY_MARKER).exists())

    def test_prepare_twice_changes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory)
            self.prepare.prepare(tree, west_pin())
            once = self.snapshot(tree)
            self.assertEqual(self.prepare.prepare(tree, west_pin()), "already prepared")
            self.assertEqual(self.snapshot(tree), once)

    def test_the_standalone_copy_is_patched_and_gets_no_marker_at_all(self):
        # scripts/test-isn-fail-closed.sh patches a bare pinned copy for native_sim. No marker format is invented for it.
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            self.assertIn("standalone", self.prepare.prepare(tree, west_pin()))
            self.assertEqual(sha((tree / SOURCE).read_bytes()), self.prepare.PATCHED_SHA256)
            self.assertEqual([p.name for p in tree.rglob(".crux-*")], [])
            # a second run cannot tell it from a copy whose marker was removed, and refuses
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())

    def test_the_tls_marker_must_be_exactly_pin_and_the_tls_hash_or_nothing_is_written(self):
        tls = self.prepare.TLS_PATCH_SHA256
        done = f"{west_pin()} {tls} {self.prepare.PATCH_SHA256}\n"
        cases = {
            "another pin": f"{'1' * 40} {tls}\n",
            "another patch hash": f"{west_pin()} {'2' * 64}\n",
            "no newline": f"{west_pin()} {tls}",
            "an extra hash before this patch": f"{west_pin()} {tls} {'3' * 64}\n",
            "the finished marker with an unpatched tcp.c": done,
            "this patch first": f"{west_pin()} {self.prepare.PATCH_SHA256}\n",
            "empty": "",
        }
        for label, text in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as directory:
                tree = self.tls_copy(directory, text=text)
                before = self.snapshot(tree)
                with self.assertRaises(ValueError):
                    self.prepare.prepare(tree, west_pin())
                self.assertEqual(self.snapshot(tree), before)

    def test_a_marker_that_is_not_a_regular_file_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.copy(directory)
            (tree / "elsewhere").write_text(f"{west_pin()} {self.prepare.TLS_PATCH_SHA256}\n")
            (tree / TLS_MARKER).symlink_to("elsewhere")
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())
            self.assertEqual((tree / SOURCE).read_bytes(), self.pinned)

    def test_a_copy_of_the_previous_marker_format_is_refused(self):
        # The earlier version of this script wrote `.crux-isn-fail-closed`. A copy with it is not the copy the freeze expects.
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory)
            (tree / LEGACY_MARKER).write_text(f"{west_pin()} {self.prepare.PATCH_SHA256}\n")
            before = self.snapshot(tree)
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())
            self.assertEqual(self.snapshot(tree), before)

    def test_a_source_with_one_changed_byte_is_refused_and_left_alone(self):
        changed = bytearray(self.pinned)
        changed[len(changed) // 2] ^= 0x01
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory, source=bytes(changed))
            marker = (tree / TLS_MARKER).read_text()
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())
            self.assertEqual((tree / SOURCE).read_bytes(), bytes(changed))
            self.assertEqual((tree / TLS_MARKER).read_text(), marker)
            self.assertFalse((tree / LEGACY_MARKER).exists())

    def test_a_patched_file_without_the_marker_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory)
            self.prepare.prepare(tree, west_pin())
            (tree / TLS_MARKER).unlink()
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())

    def test_a_patched_file_with_a_marker_of_the_tls_patch_only_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory)
            self.prepare.prepare(tree, west_pin())
            (tree / TLS_MARKER).write_text(f"{west_pin()} {self.prepare.TLS_PATCH_SHA256}\n")
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())

    def test_a_checkout_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory)
            (tree / ".git").mkdir()
            with self.assertRaises(ValueError):
                self.prepare.prepare(tree, west_pin())
            self.assertEqual((tree / SOURCE).read_bytes(), self.pinned)

    def test_a_drifted_patch_file_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = self.tls_copy(directory)
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
