"""Negative controls for the exact sample's device/init mapping witness."""
import importlib.util
from pathlib import Path
import struct
import unittest

HELPER = Path(__file__).with_name('startup_check.py')
if HELPER.exists():
    spec = importlib.util.spec_from_file_location('startup_check', HELPER)
    startup = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(startup)
else:
    startup = None

NODES = [
    ('flexspi', 'DT_N_S_soc_S_spi_402a8000', 'nxp_imx_flexspi', 'memc_flexspi'),
    ('ext_flash_ctrl', 'DT_N_S_soc_S_spi_402a8000_S_flash_controller_0',
     'nxp_imx_flexspi_nor', 'flash_flexspi_nor'),
]
SKIP = '''6000b5c0: 6860      ldr r0, [r4, #4]
6000b5c2: b130      cbz r0, 6000b5d2 <z_sys_init_run_level+0x26>
6000b5c4: 7e03      ldrb r3, [r0, #24]
6000b5c6: 07db      lsls r3, r3, #31
6000b5c8: d401      bmi.n 6000b5ce <z_sys_init_run_level+0x22>
6000b5ca: f004 ffd6 bl 6001057a <do_device_init>
6000b5ce: 3408      adds r4, #8
'''


def fixture(deferred=True):
    config = 'CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"\n'
    config += '# CONFIG_DEVICE_DEINIT_SUPPORT is not set\n'
    header = []
    if deferred:
        config += 'CONFIG_FILE_SYSTEM=y\nCONFIG_SETTINGS_NVS=y\n'
        for owner in ['file', 'scratch', 'nvs']:
            header.append(f'#define DT_N_NODELABEL_candidate_{owner}_backend DT_N_S_candidate_{owner}_backend')
    symbols = {'__init_crux_island_relocation_init': 0x1100,
               '__init_PRE_KERNEL_1_start': 0x1000, '__init_PRE_KERNEL_2_start': 0x1200,
               '__init_POST_KERNEL_start': 0x2000, '__init_APPLICATION_start': 0x3000}
    memory = {}
    for i, (label, node, compat, driver) in enumerate(NODES):
        ordinal = 777 + i * 111  # Deliberately unrelated to historical ordinals.
        header.extend([f'#define DT_N_NODELABEL_{label} {node}',
                       f'#define {node}_ORD {ordinal}', f'#define {node}_STATUS_okay 1',
                       f'#define {node}_COMPAT_MATCHES_{compat} 1',
                       f'#define {node}_P_zephyr_deferred_init {int(deferred)}'])
        dev = 0x60001000 + i * 0x40
        entry = 0x2010 + i * 8
        symbols[f'__device_dts_ord_{ordinal}'] = dev
        symbols[f'__init___device_dts_ord_{ordinal}'] = entry
        symbols[driver + '_init'] = 0x501 + i * 0x100
        symbols[driver + '_config_0'] = 0x20000010 + i * 8
        symbols[driver + '_data_0'] = 0x20000030 + i * 8
        api = 0
        if i:
            symbols[driver + '_api'] = api = 0x60002000
        memory[dev] = struct.pack('<6I', 0, symbols[driver + '_config_0'], api,
                                  0x20000100, symbols[driver + '_data_0'],
                                  symbols[driver + '_init']) + bytes([int(deferred)]) + bytes(7)
        memory[entry] = struct.pack('<2I', 0, dev)
    return '\n'.join(header) + '\n', config, symbols, memory


class StartupMapping(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(startup, 'missing semantic native init witness')

    def check(self, header, config, symbols, memory):
        return startup.native_startup_entries(header, config, symbols,
                                               lambda address, size: memory[address][:size])

    def test_deferred_devices_follow_semantic_mapping_and_exact_flags(self):
        result = self.check(*fixture())
        self.assertEqual(set(result), {'flexspi', 'ext_flash_ctrl'})
        self.assertTrue(all(device['deferred'] for device in result.values()))
        self.assertEqual(result['flexspi']['device'], 0x60001000)

    def test_known_historical_automatic_profile_is_after_verifier(self):
        result = self.check(*fixture(False))
        self.assertFalse(any(device['deferred'] for device in result.values()))

    def test_absent_altered_or_ambiguous_dt_mapping_refuses(self):
        header, config, symbols, memory = fixture()
        for changed in [header.replace('DT_N_NODELABEL_flexspi ', 'DT_N_NODELABEL_missing '),
                        header.replace('nxp_imx_flexspi 1', 'nxp_imx_flexspi 0'),
                        header.replace('DT_N_S_soc_S_spi_402a8000_ORD 777',
                                       'DT_N_S_soc_S_spi_402a8000_ORD unknown'),
                        header + '#define DT_N_NODELABEL_flexspi DT_N_S_other\n',
                        header.replace('DT_N_NODELABEL_ext_flash_ctrl DT_N_S_soc_S_spi_402a8000_S_flash_controller_0',
                                       'DT_N_NODELABEL_ext_flash_ctrl DT_N_S_other')]:
            with self.subTest(header=changed), self.assertRaises(ValueError):
                self.check(changed, config, symbols, memory)

    def test_missing_actual_device_or_init_record_refuses(self):
        for name in ['__device_dts_ord_777', '__init___device_dts_ord_888']:
            header, config, symbols, memory = fixture()
            del symbols[name]
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.check(header, config, symbols, memory)

    def test_changed_native_identity_or_entry_device_refuses(self):
        for address, offset in [(0x60001000, 4), (0x60001000, 20), (0x2010, 4)]:
            header, config, symbols, memory = fixture()
            data = bytearray(memory[address]); data[offset] ^= 1; memory[address] = data
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                self.check(header, config, symbols, memory)

    def test_dt_and_actual_flag_disagreement_or_unknown_flags_refuse(self):
        for flag in [0, 2, 3]:
            header, config, symbols, memory = fixture()
            data = bytearray(memory[0x60001000]); data[24] = flag; memory[0x60001000] = data
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                self.check(header, config, symbols, memory)

    def test_storage_profile_cannot_fall_back_to_automatic_initialization(self):
        header, config, symbols, memory = fixture()
        header = header.replace('_P_zephyr_deferred_init 1', '_P_zephyr_deferred_init 0')
        for address in [0x60001000, 0x60001040]:
            data = bytearray(memory[address]); data[24] = 0; memory[address] = data
        with self.assertRaises(ValueError):
            self.check(header, config, symbols, memory)

    def test_automatic_entry_before_verifier_or_outside_post_kernel_refuses(self):
        for early in [0x1100, 0x1108, 0x1200, 0x3000]:
            header, config, symbols, memory = fixture(False)
            memory[early] = memory[0x2010]
            symbols['__init___device_dts_ord_777'] = early
            with self.subTest(entry=early), self.assertRaises(ValueError):
                self.check(header, config, symbols, memory)

    def test_unknown_profile_or_device_layout_refuses(self):
        header, config, symbols, memory = fixture()
        for h, c in [(header.replace('DT_N_NODELABEL_candidate_file_backend', 'DT_N_NODELABEL_missing'), config),
                     (header, config.replace('# CONFIG_DEVICE_DEINIT_SUPPORT is not set',
                                              'CONFIG_DEVICE_DEINIT_SUPPORT=y')),
                     (header, config.replace('@B/', '@A/'))]:
            with self.subTest(config=c), self.assertRaises(ValueError):
                self.check(h, c, symbols, memory)

    def test_actual_kernel_deferred_skip_witness(self):
        startup.verify_deferred_skip(SKIP)

    def test_missing_wrong_flag_or_wrong_branch_skip_refuses(self):
        for body in [SKIP.replace('#24', '#28'), SKIP.replace('bmi.n', 'bpl.n'),
                     SKIP.replace('6000b5ce <', '6000b5ca <'),
                     SKIP.replace('lsls r3, r3, #31', 'lsls r3, r3, #30'),
                     SKIP.replace('<do_device_init>', '<unknown>'),
                     '6000b5be: f004 ffd6 bl 6001057a <do_device_init>\n' + SKIP, '']:
            with self.subTest(body=body), self.assertRaises(ValueError):
                startup.verify_deferred_skip(body)


if __name__ == '__main__':
    unittest.main()
