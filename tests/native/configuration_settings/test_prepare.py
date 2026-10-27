#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Destination-link regressions use fake upstream inputs only."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare


class ManifestSafety(unittest.TestCase):
    def test_manifest_symlink_preserves_external_file(self):
        self.check_link(False)

    def test_manifest_hardlink_preserves_external_file(self):
        self.check_link(True)

    def check_link(self, hardlink):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            zephyr, output = root / 'zephyr', root / 'output'
            source = zephyr / 'subsys/settings/src'
            source.mkdir(parents=True)
            for name in ['settings_store.c', 'settings_nvs.c']:
                (source / name).write_bytes(b'fake pinned settings\n')
            (output / 'nvs').mkdir(parents=True)
            (output / 'nvs/nvs.c').write_bytes(b'fake NVS candidate\n')
            victim = zephyr / 'preserve.c'
            victim.write_bytes(b'preserve fake upstream\n')
            destination = output / 'manifest.json'
            if hardlink:
                os.link(victim, destination)
            else:
                destination.symlink_to(victim)
            class Startup:
                PIN = 'fake-pin'
                SOURCE_SHA = 'fake-source-sha'
                @staticmethod
                def prepare(*_args):
                    pass
            with patch.object(prepare.importlib.util, 'module_from_spec', return_value=Startup), \
                 patch('importlib.machinery.SourceFileLoader.exec_module'), \
                 patch.object(prepare.subprocess, 'run'), \
                 patch.object(prepare.subprocess, 'check_output',
                              side_effect=['', b'fake pinned settings\n', b'fake pinned settings\n']):
                prepare.prepare(zephyr, output)
            self.assertEqual(victim.read_bytes(), b'preserve fake upstream\n')
            self.assertFalse(destination.is_symlink())
            self.assertIn('fake-pin', destination.read_text())


if __name__ == '__main__':
    unittest.main()
