"""The bench image check and profile of the RT1060-EVKB management bench image (row C8). Never registered.

The configuration fixture is an unchanged extract of the resolved ``.config`` of the real cross-build recorded in
docs/evidence/rt1060-management-image-feasibility/image-plan-c8. The ELF and BIN are synthetic layout fixtures built from
``test_m3_image`` and are never flashable; their symbol sizes are the ones that ``nm -S`` printed for the real image.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[0]
sys.path.insert(0, str(ROOT / 'target'))
sys.path.insert(0, str(HERE))
from test_m3_image import OTHER_OPTIMIZATIONS, fixture
import bench_image
import image_profile
import m5_image
import prepare_m5_network
import s2_image
from frozen_zephyr_fixtures import Frozen

FIXTURES = HERE / 'fixtures' / 'bench-image'
FACTS = json.loads((FIXTURES / 'facts.json').read_text())
CONFIG = (FIXTURES / 'bench.config').read_text()
SYMBOLS = [tuple(item) for item in FACTS['stack_symbols_nm_S']]
PLAN = (ROOT.parent / 'docs' / 'rt1060-bench-image-plan.md').read_text()
EVIDENCE = ROOT.parent / 'docs' / 'evidence' / 'rt1060-management-image-feasibility' / 'image-plan-c8' / 'build'
_SPEC = importlib.util.spec_from_file_location('recorder', ROOT.parent / 'scripts/record-build-provenance.py')
RECORDER = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(RECORDER)


def synthetic_image(root, config=CONFIG, symbols=SYMBOLS):
    """A synthetic layout whose symbol table holds exactly ``symbols`` (name, size), duplicates included."""
    binary, elf, cfg = fixture(root, 'network')
    data = bytearray(elf.read_bytes())
    names = b'\0rom_start\0.shstrtab\0.symtab\0'
    offsets = []
    for name, _ in symbols:
        offsets.append(len(names))
        names += name.encode() + b'\0'
    assert len(names) < 0x100 and 16 * (len(symbols) + 1) <= 0x100
    data[0x2200:0x2300] = names.ljust(0x100, b'\0')
    data[0x2300:0x2400] = bytes(0x100)
    for index, ((name, size), offset) in enumerate(zip(symbols, offsets), 1):
        struct.pack_into('<IIIBBH', data, 0x2300 + 16 * index, offset, 0x80000000 + 0x100 * index, size, 1, 0, 1)
    binary_size = len(binary.read_bytes())
    sections = [(0,) * 10,
                (names.index(b'rom_start'), 1, 2, 0x60000000, 0x100, binary_size, 0, 0, 4, 0),
                (names.index(b'.shstrtab'), 3, 0, 0, 0x2200, len(names), 0, 0, 1, 0),
                (names.index(b'.symtab'), 2, 0, 0, 0x2300, 16 * (len(symbols) + 1), 2, 0, 4, 16)]
    for index, section in enumerate(sections):
        struct.pack_into('<IIIIIIIIII', data, 0x2400 + 40 * index, *section)
    elf.write_bytes(data)
    cfg.write_text(config)
    return binary, elf, cfg


def without(symbol):
    return [s for s in SYMBOLS if s[0] != symbol]


def zephyr_roles(root):
    """The two frozen roles of the patched Zephyr copy (a hand-written frozen folder, see frozen_zephyr_fixtures.py)."""
    frozen = Frozen(Path(root) / 'frozen')
    return dict(bench_manifest=frozen.manifest, patched_zephyr=frozen.record_path)


def check(config=CONFIG, symbols=SYMBOLS):
    with tempfile.TemporaryDirectory() as tmp:
        return bench_image.check_image(*synthetic_image(Path(tmp), config, symbols), **zephyr_roles(tmp))


def drop(*lines):
    return '\n'.join(l for l in CONFIG.splitlines() if l not in lines) + '\n'


def swap(old, new):
    assert old in CONFIG.splitlines(), old
    return CONFIG.replace(old + '\n', new + '\n')


class Accepts(unittest.TestCase):
    def test_the_real_configuration_with_the_real_symbol_sizes_is_accepted(self):
        facts = check()
        self.assertEqual((facts['bin_address'], facts['gap_fill'], facts['elf_sections_match_bin']), (0x60000000, 255, True))
        self.assertEqual(facts['main_stack']['bytes'], 16448)
        self.assertEqual(facts['stack_objects']['worker_stacks'], [33024, 65792])
        self.assertEqual(facts['stack_objects']['_k_thread_stack_http_server_tid'], [8256])

    def test_the_fixture_is_an_unchanged_extract_of_the_recorded_configuration(self):
        real = (EVIDENCE / 'config.txt').read_text().splitlines()
        position = -1
        for line in CONFIG.splitlines():
            self.assertIn(line, real[position + 1:], line)          # same lines, same order
            position = real.index(line, position + 1)
        self.assertEqual(FACTS['image']['bin_bytes'], 562232)

    def test_the_symbol_sizes_in_the_fixture_are_those_of_the_recorded_probe(self):
        probe = (EVIDENCE.parent / 'host' / 'profile-facts.out').read_text()
        recorded = sorted((m.group(2), int(m.group(1))) for m in re.finditer(r'\s(\d+) B (\w+)\n', probe)
                          if m.group(2) in {name for name, _ in SYMBOLS})
        self.assertEqual(recorded, sorted(SYMBOLS))


class GroupMismatches(unittest.TestCase):
    """One subtest per group of section a: every line of the group, taken away or changed, is refused."""

    def refuses(self, config=CONFIG, symbols=SYMBOLS):
        with self.assertRaises(ValueError):
            check(config, symbols)

    def test_each_group_refuses_its_mismatch(self):
        groups = {
            'board': [drop('CONFIG_BOARD_REVISION="B"'), swap('CONFIG_FLASH_SIZE=8192', 'CONFIG_FLASH_SIZE=4096'),
                      drop('CONFIG_XIP=y'), drop('CONFIG_MPU_STACK_GUARD=y'), drop('CONFIG_THREAD_STACK_INFO=y')],
            'declaration and forms': [drop('CONFIG_CRUX_BENCH_TEST_HOST_KEY=y'), drop('CONFIG_CRUX_BENCH_FILE_RAM_DISK=y'),
                                      CONFIG + 'CONFIG_CRUX_BENCH_FILE_MICROSD=y\n', CONFIG + 'CONFIG_WOLFSSL_RSA=y\n'],
            'entropy': [drop('CONFIG_ENTROPY_MCUX_TRNG=y'), CONFIG + 'CONFIG_TEST_RANDOM_GENERATOR=y\n',
                        CONFIG + 'CONFIG_FAKE_ENTROPY_NATIVE_SIM=y\n'],
            'network': [swap('CONFIG_CRUX_WEB_BIND_ADDRESS="192.168.77.2"', 'CONFIG_CRUX_WEB_BIND_ADDRESS="0.0.0.0"'),
                        swap('CONFIG_CRUX_SSH_PORT=22', 'CONFIG_CRUX_SSH_PORT=2222'), CONFIG + 'CONFIG_NET_DHCPV4=y\n',
                        CONFIG + 'CONFIG_NET_IPV6=y\n', swap('CONFIG_HTTP_SERVER_MAX_CLIENTS=3', 'CONFIG_HTTP_SERVER_MAX_CLIENTS=4')],
            'buffers': [swap('CONFIG_NET_BUF_RX_COUNT=1024', 'CONFIG_NET_BUF_RX_COUNT=32'),
                        swap('CONFIG_NET_PKT_TX_COUNT=32', 'CONFIG_NET_PKT_TX_COUNT=16'),
                        swap('CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=1048576', 'CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=65536'),
                        swap('CONFIG_HTTP_SERVER_STACK_SIZE=8192', 'CONFIG_HTTP_SERVER_STACK_SIZE=2048')],
            'no card': [CONFIG + 'CONFIG_DISK_DRIVER_SDMMC=y\n', CONFIG + 'CONFIG_IMX_USDHC=y\n',
                        CONFIG + 'CONFIG_FS_FATFS_MOUNT_MKFS=y\n', drop('CONFIG_DISK_DRIVER_RAM=y'),
                        drop('CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK=y')],
            'report': [drop('CONFIG_NET_STATISTICS=y'), drop('CONFIG_CRUX_SSH_STACK_REPORT=y'),
                       drop('CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION=y'), drop('CONFIG_SYS_HEAP_RUNTIME_STATS=y'),
                       drop('CONFIG_INIT_STACKS=y'), drop('CONFIG_THREAD_NAME=y'), drop('CONFIG_THREAD_MONITOR=y')],
            'still refused': [CONFIG + 'CONFIG_%s=y\n' % f for f in
                              ('FLASH', 'FLASH_MAP', 'NVS', 'SETTINGS', 'SHELL', 'BOOTLOADER_MCUBOOT')],
            'optimisation': [CONFIG.replace(old, new) for old, new in OTHER_OPTIMIZATIONS],
        }
        for group, configs in groups.items():
            for number, config in enumerate(configs):
                with self.subTest(group=group, case=number):
                    self.refuses(config)

    def test_stack_symbol_sizes_are_enforced_as_a_set(self):
        cases = {
            'main stack': ('z_main_stack', 4160), 'http thread stack': ('_k_thread_stack_http_server_tid', 2112),
            'listener': ('listener_stack', 2112), 'report': ('report_stack', 4160),
        }
        for label, (name, size) in cases.items():
            with self.subTest(group='stack objects', case=label):
                self.refuses(symbols=[s if s[0] != name else (name, size) for s in SYMBOLS])
        for label, symbols in {
            'ssh workers only': without('worker_stacks') + [('worker_stacks', 65792)],
            'crux threads only': without('worker_stacks') + [('worker_stacks', 33024)],
            'two ssh worker blocks': without('worker_stacks') + [('worker_stacks', 65792), ('worker_stacks', 65792)],
            'no http thread stack': without('_k_thread_stack_http_server_tid'),
            'no main stack': without('z_main_stack'), 'no listener': without('listener_stack'),
            'no report stack': without('report_stack'), 'm5 observer present': SYMBOLS + [('m5_observer', 2112)],
        }.items():
            with self.subTest(group='stack objects', case=label):
                self.refuses(symbols=symbols)

    def test_a_tampered_binary_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = synthetic_image(Path(tmp))
            paths[0].write_bytes(paths[0].read_bytes()[:-1] + b'x')
            with self.assertRaises(ValueError):
                bench_image.check_image(*paths, **zephyr_roles(tmp))


class RefusesOtherImages(unittest.TestCase):
    def test_the_log_and_storage_options_that_m5_forbids_are_set_by_this_image_and_accepted(self):
        for option in ('LOG', 'FILE_SYSTEM', 'DISK_ACCESS', 'WATCHDOG'):
            self.assertIn('CONFIG_%s=y' % option, CONFIG.splitlines())
        check()

    def test_a_config_with_the_m5_constants_is_refused(self):
        m5 = '\n'.join(sorted(m5_constants())) + '\n'
        with self.assertRaises(ValueError):
            check(config=m5)

    def test_the_m5_and_s2_checks_refuse_the_bench_configuration(self):
        # the reason for a check of its own: neither existing profile check accepts this image
        with tempfile.TemporaryDirectory() as tmp:
            paths = synthetic_image(Path(tmp))
            for other in (m5_image.check_image, s2_image.check_image):
                with self.assertRaises(ValueError):
                    other(*paths)

    def test_the_synthetic_m5_network_image_is_refused_by_the_bench_check(self):
        for profile in ('network',):
            with tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(ValueError):
                    bench_image.check_image(*fixture(Path(tmp), profile), **zephyr_roles(tmp))


def m5_constants():
    source = (ROOT / 'target' / 'm5_image.py').read_text()
    block = source[source.index('required={'):source.index('require(required')]
    return re.findall(r"'(CONFIG_[A-Z0-9_]+(?:=[^']*)?)'", block)


class FirmwareIdentity(unittest.TestCase):
    def test_check_image_returns_the_identity_the_recorder_computes(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = synthetic_image(Path(tmp))
            facts = bench_image.check_image(*paths, **zephyr_roles(tmp))
            identity = dict(algorithm=RECORDER.FIRMWARE_IDENTITY_ALGORITHM, target='mimxrt1060_evk@B//qspi',
                            image_sha256=hashlib.sha256(paths[0].read_bytes()).hexdigest(),
                            configuration_sha256=hashlib.sha256(RECORDER.normalised_config(paths[2].read_bytes())).hexdigest())
            self.assertEqual(facts['firmware_identity'], RECORDER.firmware_digest(identity))

    def test_the_identity_follows_the_image_and_the_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary, elf, config = synthetic_image(Path(tmp))
            roles = zephyr_roles(tmp)
            first = bench_image.check_image(binary, elf, config, **roles)['firmware_identity']
            config.write_text(CONFIG + '# CONFIG_NOTHING is not set\n')
            self.assertNotEqual(bench_image.check_image(binary, elf, config, **roles)['firmware_identity'], first)

    def test_the_recorded_configuration_and_image_hash_reproduce_the_recorded_identity(self):
        recorded = json.loads((EVIDENCE / 'firmware-identity.json').read_text())
        self.assertEqual(FACTS['image']['firmware_identity'], recorded['firmware_identity_digest'])
        self.assertEqual(FACTS['image']['bin_sha256'], recorded['identity']['image_sha256'])
        got = bench_image.firmware_identity_of(FACTS['image']['bin_sha256'], (EVIDENCE / 'config.txt').read_bytes())
        self.assertEqual(got, FACTS['image']['firmware_identity'])


def plan_pins():
    section = PLAN[PLAN.index('### b. The manifest source and the pinned set'):PLAN.index('### c. Whether a commit tag')]
    rows = re.findall(r'^\| `([^`]+)` \| `([0-9a-f]{40})` \| `([^`]+)` \|$', section, re.M)
    return {n: r for n, r, p in rows}, {n: p for n, r, p in rows}


class Profile(unittest.TestCase):
    def setUp(self):
        self.profile = bench_image.bench_profile()

    def test_the_pinned_set_equals_the_table_of_the_plan_exactly(self):
        revisions, paths = plan_pins()
        self.assertEqual(len(revisions), 10)
        self.assertEqual(dict(self.profile.pinned), revisions)
        self.assertEqual(dict(self.profile.pinned_paths), paths)

    def test_the_fields_follow_the_profile_input_of_the_plan(self):
        p = self.profile
        self.assertEqual((p.name, p.manifest_path, p.commit_tag, p.errno_suffix, p.capture),
                         ('crux-mgmt-bench-rt1060-evkb-b', 'crux-pilot', False, None, 'raw'))
        self.assertEqual((dict(p.modules), dict(p.module_paths)), ({}, {}))
        self.assertEqual(p.tool_scripts, ('record-build-provenance.py', 'freeze-patched-zephyr.sh'))
        self.assertEqual(p.extra_roles, ('bench_manifest', 'patched_zephyr'))
        self.assertIs(p.check_image, bench_image.check_image)
        self.assertIn('crux-mgmt-bench-rt1060-evkb-b', PLAN)

    def test_the_profile_is_not_registered(self):
        self.assertNotIn(self.profile.name, prepare_m5_network.PROFILES)
        self.assertNotIn(bench_image.bench_profile, prepare_m5_network.PROFILES.values())
        self.assertEqual(sorted(prepare_m5_network.PROFILES), ['m5-network-physical', 's2-controller-physical'])
        with self.assertRaises(ValueError):
            prepare_m5_network.profile_named(self.profile.name)

    def test_the_capture_window_is_never_above_the_existing_maximum(self):
        self.assertEqual(image_profile.MAX_CAPTURE_SECONDS, 300)         # this change leaves the maximum alone
        self.assertLessEqual(self.profile.capture_seconds, image_profile.MAX_CAPTURE_SECONDS)
        self.assertEqual(bench_image.BENCH_CAPTURE_SECONDS, self.profile.capture_seconds)
        with self.assertRaises(ValueError):                              # the window the plan needs is refused
            image_profile.ImageProfile(**{**self.profile.__dict__, 'capture_seconds': 2000})

    def test_the_window_is_the_largest_the_constraints_allow_and_a_series_still_fits(self):
        self.assertEqual(self.profile.capture_seconds, 300)
        # Driver: clock + 30 s Commander limit + window < series end (900 s)
        self.assertLess(30 + self.profile.capture_seconds, 900)


if __name__ == '__main__':
    unittest.main()
