"""Actual native runtime inventory, including the unavailable GPT2 path."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SAMPLE = ROOT / 'samples/rom_observation_diagnostic'

class Runtime(unittest.TestCase):
    def test_actual_capture_keeps_pre_c_distinct_and_refuses_without_writes(self):
        source = SAMPLE / 'runtime.c'
        self.assertTrue(source.exists(), 'missing actual side-effect-free runtime inventory')
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'fsl_clock.h').write_text((HERE / 'runtime_effects.h').read_text())
            (tmp / 'fsl_gpt.h').write_text('#include "fsl_clock.h"\n')
            (tmp / 'case.c').write_text(source.read_text() + (HERE / 'runtime_cases.c').read_text())
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(tmp), '-I' + str(SAMPLE),
                            '-I' + str(ROOT / 'samples/flash_target_candidate'),
                            str(tmp / 'case.c'), '-o', str(tmp / 'case')], check=True)
            subprocess.run([str(tmp / 'case')], check=True)

    def test_shared_profile_returns_raw_inputs_even_when_unsupported(self):
        source = ROOT / 'samples/flash_target_candidate/rom_profile.c'
        import importlib.util
        spec = importlib.util.spec_from_file_location('stage', HERE / 'stage.py')
        stage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stage)
        actual = stage.diagnostic_profile(source.read_text())
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'fsl_clock.h').write_text((ROOT / 'tests/native/rom_storage_candidate/register_stubs.h').read_text())
            (tmp / 'case.c').write_text(actual + '''
#include <assert.h>
int main(void) {
    ccm.CSCMR1 = 0; flexspi.MCR0 = 0x1234;
    struct crux_rom_profile p = {0};
    assert(!crux_rom_read_capture_supported(&p));
    assert(p.cscmr1 == 0 && p.mcr0 == 0x1234 && p.root_hz == 0 && getters == 0);
    return 0;
}
''')
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(tmp), '-I' + str(source.parent),
                            str(tmp / 'case.c'), '-o', str(tmp / 'case')], check=True)
            subprocess.run([str(tmp / 'case')], check=True)
