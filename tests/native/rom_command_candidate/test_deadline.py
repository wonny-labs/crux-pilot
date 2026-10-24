"""Execute the actual private timer with controlled SDK registers/leaves."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent

class Deadline(unittest.TestCase):
    def run_case(self, case, extra="", cases=None):
        source = ROOT/'samples/flash_target_candidate/rom_deadline.c'
        self.assertTrue(source.exists(), 'missing real GPT2 software candidate')
        sdk = Path(os.environ['ZEPHYR_BASE']).parent/'modules/hal/nxp/mcux/mcux-sdk-ng'
        # Constants come from the pinned SDK, not a duplicate timer model.
        macros = ''
        for name in ['GPT', 'CCM', 'XTALOSC24M']:
            text = (sdk/f'devices/RT/RT1060/periph/PERI_{name}.h').read_text()
            macros += '\n'.join(re.findall(r'^#define (?:GPT_CR_|CCM_CCGR0_CG1[23]|XTALOSC24M_LOWPWR_CTRL_|XTALOSC24M_MISC0_OSC_XTALOK)[^\n]*', text, re.M))+'\n'
        code = (HERE/'deadline_effects.h').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp/'fsl_gpt.h').write_text(macros+code)
            (tmp/'case.c').write_text(source.read_text()+extra+(cases if cases is not None else (HERE/'deadline_cases.c').read_text()))
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror', '-Wno-unused-function',
                '-I'+str(tmp), '-I'+str(source.parent), str(tmp/'case.c'),'-o',str(tmp/'case')],check=True)
            subprocess.run([str(tmp/'case'), str(case)],check=True)
    def test_inherited_unknown_state_has_zero_configuration_writes(self): self.run_case(0)
    def test_explicit_configuration_reset_readback_and_progress(self): self.run_case(1)
    def test_declared_budget_expiry_wrap_and_stopped_counter(self): self.run_case(2)
    def test_changed_source_gate_or_configuration_latches_fault(self): self.run_case(3)

    def test_healthy_rearm_and_latched_failure(self): self.run_case(4)

    def test_no_retry_or_rearm_bypasses_prior_fault_or_active_budget(self): self.run_case(5)
