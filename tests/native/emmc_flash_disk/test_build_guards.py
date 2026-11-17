#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The build refuses what the flash-API driver cannot make safe. Needs ZEPHYR_BASE and cmake.

Each case configures (and for the partition check, builds) tests/native/emmc_flash_disk for
native_sim with one change, and checks the refusal and its message. A case that is not refused
would let an image through that has no cache flush, or a slot that shares a page with another.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

APP = os.path.dirname(os.path.abspath(__file__))
ZEPHYR_BASE = os.environ.get('ZEPHYR_BASE')


@unittest.skipUnless(ZEPHYR_BASE and shutil.which('cmake') and shutil.which('ninja'),
                     'needs ZEPHYR_BASE, cmake and ninja')
class BuildGuardTest(unittest.TestCase):
    def configure(self, *extra, overlay=None, build=False):
        out = tempfile.mkdtemp(prefix='flash-disk-guard-')
        self.addCleanup(shutil.rmtree, out, ignore_errors=True)
        env = dict(os.environ, ZEPHYR_TOOLCHAIN_VARIANT=os.environ.get('ZEPHYR_TOOLCHAIN_VARIANT',
                                                                       'host'))
        command = ['cmake', '-GNinja', '-B', out, '-S', APP, '-DBOARD=native_sim/native/64',
                   f'-DPython3_EXECUTABLE={sys.executable}']
        if overlay:
            command.append(f'-DEXTRA_DTC_OVERLAY_FILE={overlay}')
        result = subprocess.run(command + list(extra), capture_output=True, text=True, env=env)
        if build and result.returncode == 0:
            result = subprocess.run(['ninja', '-C', out], capture_output=True, text=True, env=env)
        # CMake wraps its messages, so compare the words and not the line breaks.
        return result.returncode, ' '.join((result.stdout + result.stderr).split())

    def test_the_defaults_configure(self):
        returncode, output = self.configure()
        self.assertEqual(returncode, 0, output[-1500:])

    def test_a_writing_driver_on_an_mmc_disk_needs_the_cache_flush_correction(self):
        returncode, output = self.configure('-DCONFIG_CRUX_MMC_CACHE_FLUSH=n')
        self.assertNotEqual(returncode, 0)
        self.assertIn('requires CONFIG_CRUX_MMC_CACHE_FLUSH=y', output)

    def test_a_read_only_driver_does_not_need_it(self):
        returncode, output = self.configure('-DCONFIG_CRUX_MMC_CACHE_FLUSH=n',
                                            '-DCONFIG_CRUX_FLASH_DISK_READ_ONLY=y')
        self.assertEqual(returncode, 0, output[-1500:])

    def overlay(self, text):
        with tempfile.NamedTemporaryFile('w', suffix='.overlay', delete=False) as handle:
            handle.write(text)
        self.addCleanup(os.remove, handle.name)
        return handle.name

    def test_a_partition_off_a_page_is_refused_whichever_end_is_off(self):
        for name, reg in (('start and size', '0x8200 0x7e00'), ('start only', '0x8200 0x8000'),
                          ('size only', '0x8000 0x7e00')):
            with self.subTest(name):
                returncode, output = self.configure(
                    overlay=self.overlay(f'&emmc_slot1 {{ reg = <{reg}>; }};\n'), build=True)
                self.assertNotEqual(returncode, 0)
                self.assertIn('is not on pages of erase-block-size', output)

    def test_a_write_block_other_than_one_sector_is_refused(self):
        returncode, output = self.configure(
            overlay=self.overlay('&mmcflash { write-block-size = <8>; };\n'))
        self.assertNotEqual(returncode, 0)
        self.assertIn('write-block-size', output)

    def test_an_erase_block_that_is_not_whole_sectors_or_is_zero_is_refused(self):
        for value in (100, 0):
            with self.subTest(value):
                returncode, output = self.configure(
                    overlay=self.overlay(f'&mmcflash {{ erase-block-size = <{value}>; }};\n'),
                    build=True)
                self.assertNotEqual(returncode, 0)
                self.assertTrue('erase-block-size' in output, output[-800:])


if __name__ == '__main__':
    unittest.main()
