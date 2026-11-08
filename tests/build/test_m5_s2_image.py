"""The S2 image check, run on the real resolved configuration and a synthetic ELF/BIN.

The configuration fixture is the real, normalised ``.config`` of the S2 build. The ELF and BIN are synthetic
layout fixtures from ``test_m3_image`` and are never flashable. The real ELF is internal and was not available:
the symbol facts below come from ``fixtures/s2-controller/facts.json`` and are checked against nothing real here.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[0] / 'target'))
from test_m3_image import OTHER_OPTIMIZATIONS, fixture
import s2_image

FIXTURES = HERE / 'fixtures' / 's2-controller'
FACTS = json.loads((FIXTURES / 'facts.json').read_text())
CONFIG = (FIXTURES / 's2.config').read_text()
_SPEC = importlib.util.spec_from_file_location(
    'recorder', HERE.parents[1] / 'scripts/record-build-provenance.py')
RECORDER = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(RECORDER)


def synthetic_image(root, config=CONFIG, main_stack=4160, worker_stacks=33024):
    """A synthetic image whose symbols follow the measured S2 facts and whose configuration is the real one."""
    binary, elf, cfg = fixture(root, 'network')
    data = bytearray(elf.read_bytes())
    data[:] = data.replace(b'network_stack\0', b'worker_stacks\0')     # same length: only the symbol name changes
    struct.pack_into('<I', data, 0x2328, main_stack)                    # z_main_stack
    struct.pack_into('<I', data, 0x2338, worker_stacks)                 # worker_stacks
    elf.write_bytes(data)
    cfg.write_text(config)
    return binary, elf, cfg


class FixtureFacts(unittest.TestCase):
    def test_the_committed_configuration_and_image_hash_reproduce_the_recorded_firmware_identity(self):
        normalised = hashlib.sha256((FIXTURES / 's2.config').read_bytes()).hexdigest()
        self.assertEqual(normalised, FACTS['config']['normalised_sha256'])
        self.assertEqual(RECORDER.normalised_config((FIXTURES / 's2.config').read_bytes()),
                         (FIXTURES / 's2.config').read_bytes())          # the file is already in the recorder's form
        identity = dict(algorithm=RECORDER.FIRMWARE_IDENTITY_ALGORITHM, target=FACTS['image']['firmware_identity_target'],
                        image_sha256=FACTS['image']['sha256'], configuration_sha256=normalised)
        self.assertEqual(FACTS['image']['firmware_identity_algorithm'], RECORDER.FIRMWARE_IDENTITY_ALGORITHM)
        self.assertEqual(RECORDER.firmware_digest(identity), FACTS['image']['firmware_identity'])

    def test_the_fixture_names_no_build_workspace_or_personal_path(self):
        for marker in ('/ws/', '/Users/', '/home/'):
            self.assertNotIn(marker, CONFIG)


class S2Check(unittest.TestCase):
    def check(self, **changes):
        with tempfile.TemporaryDirectory() as tmp:
            return s2_image.check_image(*synthetic_image(Path(tmp), **changes))

    def test_the_real_configuration_with_the_measured_symbols_is_accepted(self):
        result = self.check()
        self.assertEqual((result['bin_address'], result['gap_fill'], result['elf_sections_match_bin']), (0x60000000, 255, True))
        self.assertEqual((result['main_stack']['bytes'], result['worker_stacks']['bytes']), (4160, 33024))

    def test_every_required_configuration_line_is_enforced(self):
        self.assertIn('CONFIG_MAIN_STACK_SIZE=4096', s2_image.REQUIRED)
        self.assertIn('CONFIG_CRUX_CONTROLLER_PEER_ADDRESS="192.168.77.1"', s2_image.REQUIRED)
        lines = CONFIG.splitlines()
        for line in sorted(s2_image.REQUIRED):
            with self.subTest(line=line):
                self.assertIn(line, lines)                        # the real configuration holds it ...
                changed = '\n'.join(l for l in lines if l != line) + '\n'
                with self.assertRaises(ValueError):                # ... and the check refuses an image without it
                    self.check(config=changed)

    def test_a_changed_value_is_refused(self):
        for old, new in (('CONFIG_MAIN_STACK_SIZE=4096', 'CONFIG_MAIN_STACK_SIZE=4097'),
                         ('CONFIG_NET_MAX_CONTEXTS=4', 'CONFIG_NET_MAX_CONTEXTS=8'),
                         ('CONFIG_CRUX_CONTROLLER_PEER_PORT=42420', 'CONFIG_CRUX_CONTROLLER_PEER_PORT=42421'),
                         ('CONFIG_CRUX_CONTROLLER_PEER_ADDRESS="192.168.77.1"', 'CONFIG_CRUX_CONTROLLER_PEER_ADDRESS="192.168.77.9"')):
            with self.subTest(new=new):
                self.assertIn(old, CONFIG)
                with self.assertRaises(ValueError):
                    self.check(config=CONFIG.replace(old, new))

    def test_writable_storage_services_and_other_network_modes_are_refused(self):
        for feature in ('FLASH', 'FLASH_MAP', 'NVS', 'SETTINGS', 'FILE_SYSTEM', 'DISK_ACCESS', 'WATCHDOG', 'SHELL',
                        'NET_IPV6', 'NET_DHCPV4', 'BOOTLOADER_MCUBOOT'):
            with self.subTest(feature=feature), self.assertRaises(ValueError):
                self.check(config=CONFIG + 'CONFIG_%s=y\n' % feature)

    def test_the_m5_only_debug_options_are_not_required(self):
        # The M5 check demands them; S2 builds without them, with Crux Logging instead.
        for option in ('INIT_STACKS', 'PRINTK_SYNC', 'CONSOLE_GETCHAR', 'CONSOLE_SUBSYS', 'NET_BUF_POOL_USAGE'):
            self.assertNotIn('CONFIG_%s=y' % option, CONFIG.splitlines())
        self.assertIn('CONFIG_LOG=y', CONFIG.splitlines())
        self.check()

    def test_stack_objects_follow_the_measured_symbols(self):
        for changes in (dict(main_stack=4096), dict(main_stack=2112), dict(worker_stacks=33023), dict(worker_stacks=0)):
            with self.subTest(**changes), self.assertRaises(ValueError):
                self.check(**changes)

    def test_an_m5_image_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = synthetic_image(Path(tmp))
            data = bytearray(paths[1].read_bytes())
            offset = data.index(b'unused_stack\0')
            data[offset:offset + 12] = b'm5_observer\0'          # the M5 observer symbol must be absent (same length)
            paths[1].write_bytes(data)
            with self.assertRaises(ValueError):
                s2_image.check_image(*paths)

    def test_a_missing_worker_symbol_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = synthetic_image(Path(tmp))
            data = bytearray(paths[1].read_bytes())
            data[:] = data.replace(b'worker_stacks\0', b'other_stacks_\0')
            paths[1].write_bytes(data)
            with self.assertRaises(ValueError):
                s2_image.check_image(*paths)

    def test_a_tampered_binary_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = synthetic_image(Path(tmp))
            paths[0].write_bytes(paths[0].read_bytes()[:-1] + b'x')
            with self.assertRaises(ValueError):
                s2_image.check_image(*paths)

    def test_an_image_not_built_at_the_verified_level_is_refused(self):
        for old, new in OTHER_OPTIMIZATIONS:
            with self.subTest(new=new):
                self.assertIn(old, CONFIG)
                with self.assertRaises(ValueError):
                    self.check(config=CONFIG.replace(old, new))


if __name__ == '__main__':
    unittest.main()
