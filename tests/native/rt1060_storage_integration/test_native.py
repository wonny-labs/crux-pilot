"""Exact sample integration safety controls, supplementary to executable tests."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[3]
SAMPLE=ROOT/'samples/flash_target_candidate'
class Native(unittest.TestCase):
    def test_backend_init_is_deferred_and_main_remains_inert(self):
        overlay=(SAMPLE/'app.overlay').read_text()
        self.assertGreaterEqual(overlay.count('zephyr,deferred-init;'),5)
        self.assertIn('&ext_flash_ctrl { zephyr,deferred-init; };', overlay)
        self.assertIn('FS_MOUNT_FLAG_NO_FORMAT',(SAMPLE/'storage_native.c').read_text())
        self.assertNotIn('crux_candidate_fixed_trial(', (SAMPLE/'main.c').read_text())
    def test_owner_bound_native_adapter_and_refused_prerequisites_exist(self):
        source=(SAMPLE/'storage_native.c').read_text() if (SAMPLE/'storage_native.c').exists() else ''
        self.assertIn('crux_candidate_read(owner(dev)',source)
        self.assertIn('crux_candidate_write(owner(dev)',source)
        self.assertIn('crux_candidate_erase(owner(dev)',source)
        gate=(SAMPLE/'rom_command_gate.c').read_text()
        self.assertIn('crux_candidate_command_prerequisites',gate)
if __name__=='__main__':unittest.main()
