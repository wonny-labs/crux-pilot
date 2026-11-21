"""Offline oracle for the M5 physical watchdog/reset fixture (Group D, issue #30)."""
from pathlib import Path
import sys
import unittest

TARGET = Path(__file__).resolve().parents[1] / 'target'
sys.path.insert(0, str(TARGET))
import m5_watchdog_oracle as subject

ANSI = '\x1b[0m'


def line(text, stamp='00:00:00.100,000'):
    return f'[{stamp}] {ANSI}<inf> crux_logging: m5-watchdog: {text}{ANSI}\r\n'


ARM_BOOT = [
    'M5D_BOOT_V1 phase=arm cause_status=0 supported=0x7f causes=0x1 marker=none',
    'M5D_STATUS_V1 stage=initial wd_status=0 wd_state=1 timeout_ms=0',
    'M5D_WDT_START_V1 status=0 timeout_ms=2000 state=2',
    *[f'M5D_FEED_V1 index={i} status=0' for i in range(1, 6)],
    'M5D_FEED_STOP_V1 fed=5 timeout_ms=2000',
]
REPORT_BOOT = [
    'M5D_BOOT_V1 phase=report cause_status=0 supported=0x7f causes=0x8 marker=wd_armed',
    'M5D_STATUS_V1 stage=post_reset wd_status=0 wd_state=1 timeout_ms=0',
    'M5D_CLEAR_V1 status=0 readback_status=0 readback_causes=0x0',
    'M5D_RESULT_V1 result=PASS watchdog_bit=1 causes=0x8',
]


def capture(*groups, noise=''):
    return (noise + ''.join(line(t) for group in groups for t in group)).encode()


class Oracle(unittest.TestCase):
    def verdict(self, data):
        return subject.evaluate(data)

    def test_complete_two_boot_record_passes(self):
        result = self.verdict(capture(ARM_BOOT, ['*** Booting ***'][:0], REPORT_BOOT))
        self.assertEqual(result['verdict'], 'PASS', result)
        self.assertEqual(result['feeds'], 5)

    def test_missing_post_reset_boot_is_inconclusive(self):
        self.assertEqual(self.verdict(capture(ARM_BOOT))['verdict'], 'INCONCLUSIVE')

    def test_report_without_watchdog_cause_is_inconclusive(self):
        report = [REPORT_BOOT[0].replace('causes=0x8', 'causes=0x1')] + REPORT_BOOT[1:3] + \
            ['M5D_RESULT_V1 result=INCONCLUSIVE watchdog_bit=0 causes=0x1']
        result = self.verdict(capture(ARM_BOOT, report))
        self.assertEqual(result['verdict'], 'INCONCLUSIVE', result)

    def test_unsupported_watchdog_flag_is_inconclusive(self):
        report = [REPORT_BOOT[0].replace('supported=0x7f', 'supported=0x07')] + REPORT_BOOT[1:]
        self.assertEqual(self.verdict(capture(ARM_BOOT, report))['verdict'], 'INCONCLUSIVE')

    def test_second_arm_boot_means_repeated_watchdog_activation(self):
        result = self.verdict(capture(ARM_BOOT, REPORT_BOOT, ARM_BOOT))
        self.assertEqual(result['verdict'], 'FAIL', result)

    def test_feed_error_fails(self):
        arm = [t.replace('index=3 status=0', 'index=3 status=-5') for t in ARM_BOOT]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'FAIL')

    def test_start_error_fails(self):
        arm = [t.replace('status=0 timeout_ms=2000 state=2', 'status=-22 timeout_ms=2000 state=1')
               if t.startswith('M5D_WDT_START') else t for t in ARM_BOOT]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'FAIL')

    def test_wrong_feed_count_fails(self):
        arm = [t for t in ARM_BOOT if 'index=5' not in t]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'FAIL')

    def test_watchdog_not_idle_before_start_fails(self):
        arm = [t.replace('wd_state=1', 'wd_state=2') if t.startswith('M5D_STATUS_V1 stage=initial')
               else t for t in ARM_BOOT]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'FAIL')

    def test_report_watchdog_active_means_restarted_and_fails(self):
        report = [t.replace('wd_state=1', 'wd_state=2') if t.startswith('M5D_STATUS_V1 stage=post')
                  else t for t in REPORT_BOOT]
        self.assertEqual(self.verdict(capture(ARM_BOOT, report))['verdict'], 'FAIL')

    def test_truncated_record_line_is_inconclusive_not_pass(self):
        data = capture(ARM_BOOT, REPORT_BOOT)[:-30]
        self.assertEqual(self.verdict(data)['verdict'], 'INCONCLUSIVE')

    def test_empty_capture_is_inconclusive(self):
        self.assertEqual(self.verdict(b'')['verdict'], 'INCONCLUSIVE')

    def test_unrelated_noise_is_ignored(self):
        data = capture(ARM_BOOT, REPORT_BOOT, noise='\x00garbage\r\n')
        self.assertEqual(self.verdict(data)['verdict'], 'PASS')

    def test_result_line_claiming_pass_without_cause_bit_is_rejected(self):
        report = [REPORT_BOOT[0].replace('causes=0x8', 'causes=0x1')] + REPORT_BOOT[1:]
        self.assertNotEqual(self.verdict(capture(ARM_BOOT, report))['verdict'], 'PASS')

    def test_arm_boot_with_latched_watchdog_cause_is_inconclusive(self):
        arm = [t.replace('causes=0x1 marker=none', 'causes=0x9 marker=none') for t in ARM_BOOT]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'INCONCLUSIVE')

    def test_arm_boot_with_existing_marker_is_inconclusive(self):
        arm = [t.replace('marker=none', 'marker=wd_armed') for t in ARM_BOOT]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'INCONCLUSIVE')

    def test_watchdog_start_in_report_boot_fails(self):
        report = REPORT_BOOT[:2] + ['M5D_WDT_START_V1 status=0 timeout_ms=2000 state=2'] + REPORT_BOOT[2:]
        self.assertEqual(self.verdict(capture(ARM_BOOT, report))['verdict'], 'FAIL')

    def test_feed_in_report_boot_fails(self):
        report = REPORT_BOOT[:2] + ['M5D_FEED_V1 index=1 status=0'] + REPORT_BOOT[2:]
        self.assertEqual(self.verdict(capture(ARM_BOOT, report))['verdict'], 'FAIL')

    def test_missing_post_reset_status_is_inconclusive(self):
        report = [t for t in REPORT_BOOT if not t.startswith('M5D_STATUS_V1')]
        self.assertEqual(self.verdict(capture(ARM_BOOT, report))['verdict'], 'INCONCLUSIVE')

    def test_no_reset_marker_is_reported_as_watchdog_did_not_reset(self):
        result = self.verdict(capture(ARM_BOOT, ['M5D_NO_RESET_V1 waited_ms=10000']))
        self.assertEqual(result['verdict'], 'INCONCLUSIVE')
        self.assertTrue(any('did not reset' in r for r in result['reasons']), result)

    def test_feeds_before_start_fail(self):
        arm = [ARM_BOOT[0], ARM_BOOT[1]] + ARM_BOOT[3:8] + [ARM_BOOT[2], ARM_BOOT[8]]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'FAIL')

    def test_feed_after_stop_fails(self):
        arm = ARM_BOOT[:7] + [ARM_BOOT[8], ARM_BOOT[7]]
        self.assertEqual(self.verdict(capture(arm, REPORT_BOOT))['verdict'], 'FAIL')

    def test_no_reset_marker_with_later_report_boot_is_not_a_pass(self):
        arm = ARM_BOOT + ['M5D_NO_RESET_V1 waited_ms=10000']
        result = self.verdict(capture(arm, REPORT_BOOT))
        self.assertEqual(result['verdict'], 'INCONCLUSIVE', result)


if __name__ == '__main__':
    unittest.main()
