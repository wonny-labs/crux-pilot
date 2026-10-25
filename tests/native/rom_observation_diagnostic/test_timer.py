"""Actual new dispatcher/identity/SDK slot code consumes the actual GPT2 policy."""
import importlib.util
import os
import re
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tests/native/rom_command_candidate'))
import test_deadline as timer

class Timer(unittest.TestCase):
    def test_diagnostic_budget_and_each_transfer_boundary_use_actual_progress_guard(self):
        spec = importlib.util.spec_from_file_location('observation_stage', HERE / 'stage.py')
        stage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stage)
        spec = importlib.util.spec_from_file_location('definition', ROOT / 'tests/native/flash_safety/test_driver.py')
        accepted = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(accepted)
        effects = (HERE / 'effects.h').read_text()
        for name in ['crux_flash_poc_terminal', 'crux_rom_candidate_wait_check', 'crux_rom_candidate_deadline_finish']:
            match = re.search(r'^.*\b' + name + r'\([^;]*?\)\s*\{', effects, re.M)
            end = effects.index('{', match.start()) + 1
            depth = 1
            while depth:
                depth += (effects[end] == '{') - (effects[end] == '}')
                end += 1
            effects = effects[:match.start()] + effects[end:]
        effects = effects.replace('static jmp_buf terminal;', '').replace('static int reason, fail,', 'static int fail,')
        effects = effects.replace(', finishes, wait_checks;', ';')
        # Inject timer/source effects at actual provider return boundaries. The
        # actual dispatcher and deadline code must catch them, including idle.
        effects = effects.replace('    transfers++;', '''
    transfers++;
    if ((unsigned)transfers == fault_boundary) {
        if (fault_kind == 0) gpt.CNT += CRUX_OBSERVATION_TICKS;
        if (fault_kind == 1) timer_mode = 3;
        if (fault_kind == 2) ccm.CCGR0 = 0;
        if (fault_kind == 3) xtal.LOWPWR_CTRL ^= 1;
        if (fault_kind == 4) gpt.PR = 1;
        if (fault_kind == 5) gpt.CNT -= 2;
        if (fault_kind == 6) gpt.cr[0] ^= GPT_CR_EN_MASK;
    }
''')
        effects = 'static unsigned fault_boundary, fault_kind;\n' + effects
        ws = Path(os.environ['ZEPHYR_BASE']).parent
        hal = stage.COMMANDS.deadline_waits(stage.COMMANDS.BASE.transform_hal((ws / 'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text()), False)
        extra = (effects + accepted.definition(hal, 'FLEXSPI_UpdateLUT') +
                 (ROOT / 'samples/flash_target_candidate/rom_identity_memc.inc').read_text() +
                 stage.read_helpers((ws / 'zephyr/drivers/flash/flash_mcux_flexspi_nor.c').read_text()) +
                 (ROOT / 'samples/rom_observation_diagnostic/observation_nor.inc').read_text())
        setup = (ROOT / 'tests/native/rom_command_candidate/deadline_cases.c').read_text().split('int main(')[0]
        cases = setup + '''
static void reset_observation(void) {
    memset(&regs, 0, sizeof(regs));
    memset(&crux_rom_slot, 0, sizeof(crux_rom_slot));
    memcpy(regs.LUT, crux_rom_expected_lut, sizeof(regs.LUT));
    regs.LUTCR=1; regs.MCR0=0x10; regs.STS0=3; regs.FLSHCR0[0]=8192; regs.FLSHCR1[0]=0x63;
    transfers=statuses=identities=0;
    crux_observation_prepared=crux_observation_prepare_claimed=false;
    assert(crux_observation_prepare()==0);
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    for (fault_boundary=1;fault_boundary<=3;fault_boundary++) {
        for (fault_kind=0;fault_kind<7;fault_kind++) {
            initialized(); reset_observation();
            assert(crux_rom_candidate_deadline_begin(CRUX_OBSERVATION_TICKS)==0);
            terminal_reason=0;
            if (!setjmp(terminal)) {crux_rom_command_dispatch(); assert(0);}
            assert(terminal_reason==-ETIMEDOUT && crux_rom_deadline.fault);
            assert((unsigned)transfers==fault_boundary);
        }
    }
    fault_boundary=0; initialized(); reset_observation();
    assert(crux_rom_candidate_deadline_begin(CRUX_OBSERVATION_TICKS)==0);
    assert(crux_rom_command_dispatch()==0);
    assert(!crux_rom_deadline.active && !crux_rom_deadline.fault);
    assert(transfers==3 && identities==1 && !crux_rom_slot.installed);
    assert(crux_observation_result.phase==CRUX_OBSERVATION_RESTORED);
    return 0;
}
'''
        extra = extra.replace('#include "observation.h"', '#include "' + str(ROOT / 'samples/rom_observation_diagnostic/observation.h') + '"')
        extra = extra.replace('#include "rom_command.h"', '')
        timer.Deadline().run_case(0, extra, cases)
