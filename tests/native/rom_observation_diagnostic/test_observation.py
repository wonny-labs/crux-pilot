"""Execute the exact diagnostic policy with controlled context/provider leaves."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SAMPLE = ROOT / 'samples/rom_observation_diagnostic'


class Observation(unittest.TestCase):
    def test_actual_one_shot_policy_and_result_publication(self):
        source = SAMPLE / 'observation.c'
        self.assertTrue(source.exists(), 'missing diagnostic-only actual policy')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / 'case.c').write_text(source.read_text() + (HERE / 'policy_cases.c').read_text())
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(SAMPLE), '-I' + str(ROOT / 'samples/flash_target_candidate'),
                            str(tmp / 'case.c'), '-o', str(tmp / 'case')], check=True)
            for case in range(10):
                subprocess.run([str(tmp / 'case'), str(case)], check=True)

    def test_invalid_pre_c_snapshot_never_reaches_runtime_or_timer(self):
        source = SAMPLE / 'observation.c'
        self.assertTrue(source.exists())
        code = source.read_text() + (HERE / 'policy_cases.c').read_text().replace('0x50494c54, 1', '0, 1')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / 'case.c').write_text(code)
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(SAMPLE), '-I' + str(ROOT / 'samples/flash_target_candidate'),
                            str(tmp / 'case.c'), '-o', str(tmp / 'case')], check=True)
            subprocess.run([str(tmp / 'case'), '10'], check=True)

    def test_actual_status_id_only_sequence_and_ram_refusals(self):
        source = SAMPLE / 'observation_nor.inc'
        self.assertTrue(source.exists(), 'missing bounded status/identity dispatcher')
        import importlib.util
        spec = importlib.util.spec_from_file_location('stage_observation', HERE / 'stage.py')
        stage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stage)
        workspace = Path(os.environ['ZEPHYR_BASE']).parent
        helpers = stage.read_helpers((workspace / 'zephyr/drivers/flash/flash_mcux_flexspi_nor.c').read_text())
        accepted_spec = importlib.util.spec_from_file_location('accepted', ROOT / 'tests/native/flash_safety/test_driver.py')
        accepted = importlib.util.module_from_spec(accepted_spec)
        accepted_spec.loader.exec_module(accepted)
        hal = stage.COMMANDS.deadline_waits(stage.COMMANDS.BASE.transform_hal(
            (workspace / 'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text()), False)
        # Effects below drive the actual native status/ID helpers, actual SDK LUT
        # update, actual selected identity checks, and actual new dispatcher.
        effects = (HERE / 'effects.h').read_text()
        code = (effects + accepted.definition(hal, 'FLEXSPI_UpdateLUT') +
                (ROOT / 'samples/flash_target_candidate/rom_identity_memc.inc').read_text() +
                helpers + source.read_text() + (HERE / 'dispatch_cases.c').read_text())
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / 'case.c').write_text(code)
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-I' + str(SAMPLE), '-I' + str(ROOT / 'samples/flash_target_candidate'),
                            '-I' + str(ROOT / 'tests/native/storage_target_guard'),
                            str(tmp / 'case.c'), '-o', str(tmp / 'case')], check=True)
            for case in range(8):
                subprocess.run([str(tmp / 'case'), str(case)], check=True)


if __name__ == '__main__':
    unittest.main()
