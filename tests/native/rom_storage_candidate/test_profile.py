"""ROM-preserving candidate checks; never accesses hardware."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SAMPLE = ROOT / 'samples/flash_target_candidate'
WORKSPACE = Path(os.environ['ZEPHYR_BASE']).parent
FCB = Path('zephyr/boards/nxp/mimxrt1060_evk/xip/evkbmimxrt1060_flexspi_nor_config.c')

class FcbStaging(unittest.TestCase):
    def stage(self):
        path = ROOT / 'tests/native/flash_target_candidate/stage_fcb.py'
        self.assertTrue(path.exists(), 'missing guarded 100 MHz FCB staging')
        spec = importlib.util.spec_from_file_location('stage_fcb', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_only_clock_initializer_changes_and_manifest_identifies_inputs(self):
        stage = self.stage()
        with tempfile.TemporaryDirectory() as tmp:
            meta = stage.prepare(WORKSPACE, Path(tmp))
            output = Path(tmp) / FCB.name
            original = (WORKSPACE / FCB).read_text()
            candidate = output.read_text().split('/* Candidate layout witnesses. */')[0]
            self.assertEqual(candidate, original.replace('FLEXSPI_SERIAL_CLK_120MHZ', 'FLEXSPI_SERIAL_CLK_100MHZ'))
            self.assertEqual(meta[FCB.name]['source_sha256'], '0ea7cf9246bac6643cad06396ff3f518ebf34b7793666646847e736eacb8c5f3')
            self.assertTrue((Path(tmp) / 'fcb-source-hashes.json').exists())

    def test_wrong_source_refuses_before_publication(self):
        stage = self.stage()
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / 'ws'
            source = workspace / FCB
            source.parent.mkdir(parents=True)
            source.write_text((WORKSPACE / FCB).read_text() + '/* drift */')
            with self.assertRaises(ValueError):
                stage.prepare(workspace, Path(tmp) / 'out')
            self.assertFalse((Path(tmp) / 'out').exists())

    def test_upstream_output_refused_and_existing_links_cannot_change_victims(self):
        stage = self.stage()
        with self.assertRaises(ValueError):
            stage.prepare(WORKSPACE, WORKSPACE / 'zephyr/candidate-out')
        for hard in [False, True]:
            with self.subTest(hard=hard), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / 'out'; out.mkdir()
                victim = Path(tmp) / 'victim'; victim.write_text('retained')
                for name in [FCB.name, 'fcb-source-hashes.json']:
                    if hard:
                        os.link(victim, out / name)
                    else:
                        (out / name).symlink_to(victim)
                stage.prepare(WORKSPACE, out)
                self.assertEqual(victim.read_text(), 'retained')
                self.assertFalse((out / FCB.name).is_symlink())

class Profile(unittest.TestCase):
    def test_declared_profile_and_each_wrong_input(self):
        self.assertTrue((SAMPLE / 'rom_profile.h').exists(), 'missing consumed ROM profile predicate')
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'case.c'; exe = Path(tmp) / 'case'
            source.write_text((ROOT / 'tests/native/rom_storage_candidate/profile_cases.c').read_text())
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(SAMPLE), str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

class ActualAdapter(unittest.TestCase):
    def test_actual_adapter_refuses_unsafe_clock_before_getter_and_does_not_write(self):
        adapter = SAMPLE / 'rom_profile.c'
        self.assertTrue(adapter.exists(), 'missing actual read-confirm adapter')
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'fsl_clock.h').write_text((ROOT / 'tests/native/rom_storage_candidate/register_stubs.h').read_text())
            source = tmp / 'adapter_case.c'
            source.write_text('#include "' + str(adapter) + '"\n' +
                              (ROOT / 'tests/native/rom_storage_candidate/adapter_cases.c').read_text())
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(tmp), str(source), '-o', str(tmp / 'case')], check=True)
            subprocess.run([str(tmp / 'case')], check=True)

if __name__ == '__main__':
    unittest.main()
