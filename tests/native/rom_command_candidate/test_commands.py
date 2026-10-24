"""Execute exact staged NOR helpers/dispatcher with controlled hardware leaves."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
STAGE = ROOT / 'tests/native/flash_target_candidate/stage_commands.py'

class Commands(unittest.TestCase):
    def code(self):
        self.assertTrue(STAGE.exists(), 'missing isolated ROM command staging')
        spec = importlib.util.spec_from_file_location('commands', STAGE)
        stage = importlib.util.module_from_spec(spec); spec.loader.exec_module(stage)
        nor = (Path(os.environ['ZEPHYR_BASE']) / 'drivers/flash/flash_mcux_flexspi_nor.c').read_text()
        return ((HERE / 'effects.h').read_text() + stage.selected_helpers(nor) +
                (ROOT/'samples/flash_target_candidate/rom_command_nor.inc').read_text() +
                (HERE/'cases.c').read_text())
    def run_case(self, *args):
        code = self.code()
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'case.c'; exe=Path(tmp)/'case'; source.write_text(code)
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-Wno-unused-function',
                '-I'+str(ROOT/'samples/flash_target_candidate'),
                '-I'+str(ROOT/'tests/native/storage_target_guard'),str(source),'-o',str(exe)],check=True)
            return subprocess.check_output([str(exe),*map(str,args)],text=True).strip()
    def test_initial_busy_or_failed_status_cannot_mutate(self):
        for failure in [5,10]:
            value=self.run_case(1,0,failure).split()
            self.assertEqual(value[0], '777')
            self.assertNotIn('3,',value[2]); self.assertNotIn('9,',value[2])
            self.assertNotIn('R',value[2])

    def test_initial_read_busy_or_status_error_cannot_read_data(self):
        for failure in [5,10]:
            value=self.run_case(3,0,failure).split()
            self.assertEqual(value[0],'777')
            self.assertNotIn('0,',value[2])

    def test_program_uses_rom_indices_and_copied_payload(self):
        self.assertEqual(self.run_case(1,0,0), '0 0 1,1,I,?,1,I,3,1,9,1,I,R,I, 90')
    def test_sector_and_read_status_mapping(self):
        self.assertEqual(self.run_case(2,0,0), '0 0 1,1,I,?,1,I,3,1,5,1,I,R,I, 0')
        self.assertEqual(self.run_case(3,0,0), '0 0 1,1,I,0,1,I, 0')
        self.assertEqual(self.run_case(4,0,0), '0 0 1,1,I, 0')
    def test_invalid_requests_emit_no_commands(self):
        for case in range(1,14):
            with self.subTest(case=case):
                self.assertEqual(self.run_case(1,case,0), '-13 0  0')
    def test_transfer_errors_are_terminal_without_refresh(self):
        for fail, trace, programmed in [(10,'1,',0),(11,'1,1,',0),
                            (12,'1,1,I,?,',0),(13,'1,1,I,?,1,',0),
                            (14,'1,1,I,?,1,I,3,',0),
                            (30,'1,1,I,?,1,I,3,1,',0),
                            (31,'1,1,I,?,1,I,3,1,9,',90),
                            (32,'1,1,I,?,1,I,3,1,9,1,',90)]:
            self.assertEqual(self.run_case(1,0,fail), f'777 -5 {trace} {programmed}')
    def test_stuck_wip_and_deadline_never_refresh_or_return(self):
        value=self.run_case(1,0,5).split()
        self.assertEqual(value[:2],['777','-110']); self.assertNotIn('R',value[2])
        self.assertNotIn('I',value[2])
        self.assertEqual(self.run_case(1,0,6), '777 -110  0')
        value=self.run_case(1,0,15).split()
        self.assertEqual(value[:2],['777','-110']); self.assertIn('3,1,9,',value[2])
        self.assertNotIn('R',value[2])
        self.assertEqual(self.run_case(1,0,16), '777 -110 1,1,I,?,1,I,3, 0')
    def test_idle_and_refresh_uncertainty_never_return(self):
        self.assertEqual(self.run_case(1,0,7), '777 -110 1,1,I, 0')
        self.assertEqual(self.run_case(1,0,8), '777 -5 1,1,I,?,1,I,3,1,9,1,I,R, 90')
        self.assertEqual(self.run_case(1,0,9), '777 -110 1,1,I,?,1,I,3,1,9,1,I, 90')
        self.assertEqual(self.run_case(1,0,24), '777 -110 1,1,I,?,1,I,3,1,9,1,I,R,I, 90')

if __name__=='__main__': unittest.main()
