"""Actual pinned identity helper/SDK LUT update in a controlled RAM composition."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
class Identity(unittest.TestCase):
    def source(self):
        spec=importlib.util.spec_from_file_location('stage',ROOT/'tests/native/flash_target_candidate/stage_commands.py')
        stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)
        spec=importlib.util.spec_from_file_location('accepted',ROOT/'tests/native/flash_safety/test_driver.py')
        accepted=importlib.util.module_from_spec(spec);spec.loader.exec_module(accepted)
        ws=Path(os.environ['ZEPHYR_BASE']).parent
        helpers=stage.selected_helpers((ws/'zephyr/drivers/flash/flash_mcux_flexspi_nor.c').read_text())
        self.assertIn('crux_rom_native_read_id_helper',helpers,'actual pinned identity helper not selected')
        hal=stage.deadline_waits(stage.BASE.transform_hal((ws/'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text()),False)
        return ((HERE/'identity_effects.h').read_text()+accepted.definition(hal,'FLEXSPI_UpdateLUT')+
                (ROOT/'samples/flash_target_candidate/rom_identity_memc.inc').read_text()+helpers+
                (ROOT/'samples/flash_target_candidate/rom_command_nor.inc').read_text()+
                (HERE/'identity_cases.c').read_text())
    def run_case(self, case):
        source=self.source()
        with tempfile.TemporaryDirectory() as tmp:
            c=Path(tmp)/'case.c';exe=Path(tmp)/'case';c.write_text(source)
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-Wno-unused-function',
                '-I'+str(ROOT/'samples/flash_target_candidate'),'-I'+str(ROOT/'tests/native/storage_target_guard'),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe),str(case)],check=True)
    def test_identity_restores_exact_table_lock_and_preserves_payload(self): self.run_case(0)
    def test_each_inherited_table_word_refuses_before_any_command(self): self.run_case(1)
    def test_ranges_other_ports_active_ip_and_lock_refuse_before_commands(self): self.run_case(2)
    def test_unknown_family_density_zero_ff_cannot_mutate(self): self.run_case(3)
    def test_install_read_status_restore_and_unowned_change_terminal(self): self.run_case(4)
    def test_wel_clear_or_busy_cannot_program(self): self.run_case(5)
    def test_idle_stale_ipcr1_is_not_an_active_reservation(self): self.run_case(6)
