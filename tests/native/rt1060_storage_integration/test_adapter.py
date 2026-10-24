"""Bounded adapter and actual command read delivery; controlled command leaves only."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
SAMPLE = ROOT / 'samples/flash_target_candidate'
HERE = Path(__file__).resolve().parent

class Adapter(unittest.TestCase):
    def compile_run(self, text, *sources):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'case.c'; exe = Path(tmp) / 'case'
            source.write_text(text)
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-I'+str(SAMPLE),
                '-I'+str(ROOT/'tests/native/storage_target_guard'), str(source),
                *map(str, sources), '-o', str(exe)], check=True)
            for case in range(7):
                subprocess.run([str(exe), str(case)], check=True)

    def test_owner_bounds_splits_partial_failure_and_one_way_lifecycle(self):
        self.assertTrue((SAMPLE/'storage_adapter.c').exists(), 'missing bounded adapter')
        self.compile_run((HERE/'adapter_cases.c').read_text(), SAMPLE/'storage_adapter.c')

    def test_actual_target_leaves_refuse_without_initialization_or_commands(self):
        self.compile_run((SAMPLE/'rom_command_gate.c').read_text() + r"""
#include <assert.h>
int main(void) {
    assert(crux_candidate_command_prerequisites() == -EACCES);
    assert(crux_rom_command_submit(NULL) == -EACCES);
    assert(crux_rom_elapsed_expired());
    return 0;
}
""")

    def test_actual_read_result_only_after_ready_completion_and_consumed_once(self):
        header = (SAMPLE/'rom_command.h').read_text()
        self.assertIn('crux_rom_command_read_result', header, 'read result is currently discarded')
        spec = importlib.util.spec_from_file_location('stage', ROOT/'tests/native/flash_target_candidate/stage_commands.py')
        stage = importlib.util.module_from_spec(spec); spec.loader.exec_module(stage)
        nor = (Path(os.environ['ZEPHYR_BASE'])/'drivers/flash/flash_mcux_flexspi_nor.c').read_text()
        effects = (ROOT/'tests/native/rom_command_candidate/effects.h').read_text()
        # Controlled transfer delivers bytes; all native helpers and dispatcher remain actual.
        effects = effects.replace('if (t->seqIndex == 1) {',
            'if (t->seqIndex == 0) memset(t->data, 0xa5, t->dataSize);\n    if (t->seqIndex == 1) {')
        code = effects + stage.selected_helpers(nor) + (SAMPLE/'rom_command_nor.inc').read_text()
        self.compile_run(code + (HERE/'read_cases.c').read_text())

if __name__ == '__main__': unittest.main()
