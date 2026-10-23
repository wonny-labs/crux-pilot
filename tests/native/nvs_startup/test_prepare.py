#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Safety regression for disposable-copy destination selection; no real Zephyr edits."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare


class DestinationSafety(unittest.TestCase):
    def test_checkout_destination_is_rejected(self):
        self.check_destination(False)

    def test_symlink_into_checkout_is_rejected(self):
        self.check_destination(True)

    def test_existing_candidate_symlink_preserves_checkout(self):
        self.check_existing_link(False)

    def test_existing_candidate_hardlink_preserves_checkout(self):
        self.check_existing_link(True)

    def check_existing_link(self, hardlink):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            zephyr = root / 'zephyr'
            source = zephyr / 'subsys/kvss/nvs/nvs.c'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'fake pinned source\n')
            victim = zephyr / 'other.c'
            victim.write_bytes(b'preserve this unrelated upstream file\n')
            output = root / 'output'
            output.mkdir()
            candidate = output / 'nvs.c'
            if hardlink:
                os.link(victim, candidate)
            else:
                candidate.symlink_to(victim)
            with patch.object(prepare, 'SOURCE_SHA', hashlib.sha256(source.read_bytes()).hexdigest()), \
                 patch.object(prepare.subprocess, 'check_output', side_effect=[prepare.PIN, '']), \
                 patch.object(prepare.subprocess, 'run'):
                prepare.prepare(zephyr, output)
            self.assertEqual(victim.read_bytes(), b'preserve this unrelated upstream file\n')
            self.assertEqual(candidate.read_bytes(), source.read_bytes())

    def check_destination(self, alias):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            zephyr = root / 'zephyr'
            source = zephyr / 'subsys/kvss/nvs/nvs.c'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'unchanged fake pinned source\n')
            destination = source.parent
            if alias:
                destination = root / 'alias'
                destination.symlink_to(source.parent, target_is_directory=True)
            original = source.read_bytes()
            with patch.object(prepare, 'SOURCE_SHA', hashlib.sha256(original).hexdigest()), \
                 patch.object(prepare.subprocess, 'check_output', side_effect=[prepare.PIN, '']), \
                 patch.object(prepare.subprocess, 'run') as apply_patch:
                with self.assertRaisesRegex(RuntimeError, 'outside'):
                    prepare.prepare(zephyr, destination)
                apply_patch.assert_not_called()
            self.assertEqual(source.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
