"""Actual generated subset and diagnostic HAL admission effects."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('observation_stage', HERE / 'stage.py')
stage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stage)

class Stage(unittest.TestCase):
    def test_exact_subset_and_manifest(self):
        import hashlib
        workspace = Path(os.environ['ZEPHYR_BASE']).parent
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            metadata = stage.prepare(workspace, output)
            for name in metadata:
                self.assertEqual(metadata[name]['diagnostic_sha256'], hashlib.sha256((output / name).read_bytes()).hexdigest())
            hal = (output / 'fsl_flexspi.c').read_text()
            for forbidden in ['FLEXSPI_WriteBlocking(', 'FLEXSPI_SoftwareReset(', 'FLEXSPI_Init(']:
                self.assertNotIn(forbidden, hal)
            nor = (output / 'observation_native.c').read_text()
            for forbidden in ['write_enable(', 'page_program(', 'erase_sector(', 'flash_flexspi_nor_init(', 'DEVICE_DT_INST_DEFINE', 'probe_lut']:
                self.assertNotIn(forbidden, nor)
            self.assertIn('crux_rom_native_read_status', nor)
            self.assertIn('crux_rom_native_read_id_helper', nor)
            memc = (output / 'memc_mcux_flexspi.c').read_text()
            for forbidden in ['memc_flexspi_init(', 'DEVICE_DT_INST_DEFINE', 'memc_flexspi_update_clock(']:
                self.assertNotIn(forbidden, memc)

    def test_upstream_and_changed_pins_refuse_before_output(self):
        workspace = Path(os.environ['ZEPHYR_BASE']).parent
        with self.assertRaises(ValueError):
            stage.prepare(workspace, workspace / 'zephyr/observation-out')
        with self.assertRaises(ValueError):
            stage.read_helpers((workspace / 'zephyr/drivers/flash/flash_mcux_flexspi_nor.c').read_text() + '/* drift */')

    def test_actual_hal_refuses_arbitrary_reads_writes_and_all_other_sequences(self):
        workspace = Path(os.environ['ZEPHYR_BASE']).parent
        hal = stage.diagnostic_hal((workspace / 'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text())
        spec = importlib.util.spec_from_file_location('definition', ROOT / 'tests/native/flash_safety/test_driver.py')
        accepted = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(accepted)
        stubs = (ROOT / 'tests/native/flash_target_candidate/hal_stubs.h').read_text()
        code = stubs + (HERE / 'hal_effects.h').read_text() + accepted.definition(hal, 'FLEXSPI_TransferBlocking') + (HERE / 'hal_cases.c').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'case.c').write_text(code)
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-I' + str(ROOT / 'samples/flash_target_candidate'), str(tmp / 'case.c'),
                            '-o', str(tmp / 'case')], check=True)
            subprocess.run([str(tmp / 'case')], check=True)
