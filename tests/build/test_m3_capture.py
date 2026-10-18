import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "target"))

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import capture_m3 as runner


class RunnerControls(unittest.TestCase):
    def test_expected_exit_on_error_configuration_banner_is_informational(self):
        with tempfile.TemporaryDirectory() as d:
            def fake_run(*args, **kwargs):
                kwargs['stdout'].write(b'J-Link Commander will now exit on Error\nScript processing completed.\n')
            with patch.object(runner.subprocess, 'run', side_effect=fake_run):
                text = runner.commander('602003106', 'connect\nq\n', Path(d) / 'probe.log')
            self.assertIn('Script processing completed.', text)

    def test_probe_diagnostic_even_with_zero_exit_stops(self):
        for diagnostic in ['****** Error: operation failed', 'Could not start CPU core. (ErrorCode: -1)',
                           'WARNING: unexpected target', 'Failed to connect', 'Cannot read memory']:
            with self.subTest(diagnostic=diagnostic), tempfile.TemporaryDirectory() as d:
                def fake_run(*args, **kwargs):
                    kwargs['stdout'].write(diagnostic.encode())
                with patch.object(runner.subprocess, 'run', side_effect=fake_run), self.assertRaises(runner.HardwareStop):
                    runner.commander('602003106', 'connect\nq\n', Path(d) / 'probe.log')

    def test_cleanup_failure_cannot_mask_probe_stop(self):
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d) / 'trial'
            with patch.object(runner, 'Receiver') as receiver, patch.object(runner, 'drain'), \
                 patch.object(runner, 'commander', side_effect=runner.HardwareStop('probe stopped')):
                receiver.return_value.finish.side_effect = ValueError('partial final serial line')
                receiver.return_value.rows = []
                receiver.return_value.text.return_value = ''
                with self.assertRaises(runner.HardwareStop):
                    runner.capture(None, '602003106', directory, '0123456789ab', True)
            self.assertEqual(json.loads((directory / 'result.json').read_text())['error_type'], 'HardwareStop')
            self.assertTrue((directory / 'reader-stop-error.txt').exists())

    def test_evidence_write_failure_cannot_trigger_programming_after_probe_stop(self):
        for name in ('reader-stop-error.txt', 'transmit.json', 'lines.json', 'uart.txt', 'failure.txt', 'result.json'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as d:
                directory = Path(d) / 'trial'
                events = []
                original_write = Path.write_text

                def write(path, *args, **kwargs):
                    if path.name == name:
                        raise OSError('controlled evidence write failure')
                    return original_write(path, *args, **kwargs)

                def observe(stage, fault):
                    events.append(stage)
                    if fault:
                        runner.capture(None, '602003106', directory, '0123456789ab', True)

                with patch.object(runner, 'Receiver') as receiver, patch.object(runner, 'drain'), \
                     patch.object(runner, 'commander', side_effect=runner.HardwareStop('probe stopped')), \
                     patch.object(Path, 'write_text', write):
                    receiver.return_value.finish.side_effect = ValueError('incomplete line')
                    receiver.return_value.rows = []
                    receiver.return_value.text.return_value = ''
                    with self.assertRaises(runner.HardwareStop):
                        runner.run_series(lambda role, stage: events.append(stage), observe)
                self.assertEqual(events, ['program-normal-preflight', 'normal-preflight', 'program-probe', 'trial-01'])

    def test_successful_series_has_three_trials_and_final_normal(self):
        events = []
        runner.run_series(lambda role, name: events.append((role, name)),
                          lambda name, fault: events.append((fault, name)))
        self.assertEqual(events, [('normal', 'program-normal-preflight'), (False, 'normal-preflight'),
                                 ('probe', 'program-probe'), (True, 'trial-01'), (True, 'trial-02'),
                                 (True, 'trial-03'), ('normal', 'program-final-normal'), (False, 'final-normal')])

    def test_runtime_failure_stops_injections_and_recovers_once(self):
        events = []
        def observe(name, fault):
            events.append(name)
            if fault:
                raise ValueError('Rejected runtime evidence')
        with self.assertRaises(ValueError):
            runner.run_series(lambda role, name: events.append(name), observe)
        self.assertEqual(events, ['program-normal-preflight', 'normal-preflight', 'program-probe',
                                 'trial-01', 'program-final-normal', 'final-normal'])

    def test_probe_or_program_failure_stops_all_further_hardware(self):
        for fail_at in ['program-normal-preflight', 'program-probe', 'trial-01', 'program-final-normal']:
            events = []
            def action(name):
                events.append(name)
                if name == fail_at:
                    raise runner.HardwareStop(name)
            with self.subTest(fail_at=fail_at), self.assertRaises(runner.HardwareStop):
                runner.run_series(lambda role, name: action(name), lambda name, fault: action(name))
            self.assertEqual(events[-1], fail_at)

    def test_unlisted_binary_rejected_before_image_or_hardware(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            binary = root / 'unlisted.bin'
            binary.write_bytes(b'changed bytes')
            m = dict(target='mimxrt1060_evk@B//qspi', bin_address=0x60000000,
                     elf_segments_match_bin=True, source={'dirty': False, 'recoverable_from_git': True},
                     profile='probe', binary=str(binary), elf=str(root / 'missing.elf'),
                     config=str(root / '.config'), artifacts=[])
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps(m))
            with patch.object(runner, 'check_image') as image_check, self.assertRaises(ValueError):
                runner.verify_manifest(manifest)
            image_check.assert_not_called()

    def test_normal_recovery_cannot_filter_away_unhealthy_interval(self):
        rows = [(0, '*** Booting Zephyr OS build v4.4.0 ***'),
                (.01, 'M3_BOOT source=0123456789ab profile=independent-health commands=0 fault_default=off')]
        rows.append((.02, 'M3_RESET support_status=0 supported=0x1f cause_status=0 cause=0x1 clear_status=0 remaining_status=0 remaining=0x0'))
        for n in range(1, 5):
            rows.append((n+.001, f'M3_WORK sequence={n*10} monotonic_ms={n*1000} time=-4 pressed=0 device=0'))
            rows.append((n, f'M3_OBSERVER_ALIVE monotonic_ms={n*1000} healthy=1 sequence={n*10} readiness=0'))
        rows += [(t+.01, 'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=3000 main_status=0 main_usable=2048 main_unused=1500') for t, s in rows if s.startswith('M3_OBSERVER_ALIVE ')]
        rows.sort(key=lambda row: row[0])
        self.assertEqual(runner.normal_result(rows, '0123456789ab')['status'], 'PASS')
        rows.insert(4, (2.5, 'M3_OBSERVER_ALIVE monotonic_ms=2500 healthy=0 sequence=20 readiness=0'))
        with self.assertRaises(ValueError):
            runner.normal_result(rows, '0123456789ab')


if __name__ == '__main__':
    unittest.main()
