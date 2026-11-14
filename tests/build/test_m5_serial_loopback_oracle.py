"""Offline oracle for the M5 physical Serial fixture (Group B, issue #27), internal loopback."""
from pathlib import Path
import sys
import unittest

TARGET = Path(__file__).resolve().parents[1] / 'target'
sys.path.insert(0, str(TARGET))
import m5_serial_loopback_oracle as subject

ANSI = '\x1b[0m'


def line(text):
    return f'[00:00:00.100,000] {ANSI}<inf> crux_logging: m5-serial: {text}{ANSI}\r\n'


GOOD = [
    'M5B_BEGIN_V1 target=evkb_b port=lpuart3 mode=internal_loopback baud=115200 open_status=0',
    'M5B_XFER_V1 seq=1 write_status=0 written=16 read_status=0 read=16 match=1',
    'M5B_TIMEOUT_V1 read_status=-7 transferred=0 timeout_ms=300 elapsed_ms=301',
    'M5B_XFER_V1 seq=2 write_status=0 written=16 read_status=0 read=16 match=1',
    'M5B_CLOSE_V1 close_status=0 reopen_status=0 reclose_status=0',
    'M5B_RESULT_V1 result=PASS limits=internal_loopback_pins_not_exercised',
]


def capture(records, noise=''):
    return (noise + ''.join(line(t) for t in records)).encode()


def mutate(old, new):
    return [t.replace(old, new) for t in GOOD]


class Oracle(unittest.TestCase):
    def verdict(self, records, **kw):
        return subject.evaluate(capture(records, **kw))

    def test_complete_loopback_passes_and_is_marked_limited(self):
        result = self.verdict(GOOD)
        self.assertEqual(result['verdict'], 'PASS', result)
        self.assertTrue(result['limited'])

    def test_empty_capture_is_inconclusive(self):
        self.assertEqual(subject.evaluate(b'')['verdict'], 'INCONCLUSIVE')

    def test_missing_result_line_is_inconclusive(self):
        self.assertEqual(self.verdict(GOOD[:-1])['verdict'], 'INCONCLUSIVE')

    def test_truncated_final_line_is_inconclusive(self):
        self.assertEqual(subject.evaluate(capture(GOOD)[:-25])['verdict'], 'INCONCLUSIVE')

    def test_timeout_that_returns_data_fails(self):
        self.assertEqual(self.verdict(mutate('read_status=-7 transferred=0',
                                             'read_status=0 transferred=3'))['verdict'], 'FAIL')

    def test_timeout_wrong_status_fails(self):
        self.assertEqual(self.verdict(mutate('read_status=-7', 'read_status=-5'))['verdict'], 'FAIL')

    def test_timeout_returning_too_early_fails(self):
        self.assertEqual(self.verdict(mutate('elapsed_ms=301', 'elapsed_ms=10'))['verdict'], 'FAIL')

    def test_timeout_that_hangs_fails(self):
        self.assertEqual(self.verdict(mutate('elapsed_ms=301', 'elapsed_ms=5000'))['verdict'], 'FAIL')

    def test_payload_mismatch_fails(self):
        self.assertEqual(self.verdict(mutate('seq=1 write_status=0 written=16 read_status=0 read=16 match=1',
                                             'seq=1 write_status=0 written=16 read_status=0 read=16 match=0'))['verdict'], 'FAIL')

    def test_short_read_fails(self):
        self.assertEqual(self.verdict(mutate('seq=2 write_status=0 written=16 read_status=0 read=16',
                                             'seq=2 write_status=0 written=16 read_status=0 read=9'))['verdict'], 'FAIL')

    def test_write_error_fails(self):
        self.assertEqual(self.verdict(mutate('seq=2 write_status=0', 'seq=2 write_status=-6'))['verdict'], 'FAIL')

    def test_recovery_transaction_missing_is_not_pass(self):
        records = [t for t in GOOD if 'seq=2' not in t]
        self.assertNotEqual(self.verdict(records)['verdict'], 'PASS')

    def test_order_must_be_transfer_timeout_transfer(self):
        records = [GOOD[0], GOOD[2], GOOD[1], GOOD[3], GOOD[4], GOOD[5]]
        self.assertEqual(self.verdict(records)['verdict'], 'FAIL')

    def test_close_or_reopen_error_fails(self):
        self.assertEqual(self.verdict(mutate('reopen_status=0', 'reopen_status=-6'))['verdict'], 'FAIL')

    def test_unexpected_mode_fails(self):
        self.assertEqual(self.verdict(mutate('mode=internal_loopback', 'mode=external'))['verdict'], 'FAIL')

    def test_result_pass_without_limits_label_is_rejected(self):
        self.assertNotEqual(self.verdict(mutate(' limits=internal_loopback_pins_not_exercised', ''))['verdict'],
                            'PASS')

    def test_duplicate_run_fails(self):
        self.assertEqual(self.verdict(GOOD + GOOD)['verdict'], 'FAIL')

    def test_noise_is_ignored(self):
        self.assertEqual(self.verdict(GOOD, noise='\x00junk\r\n')['verdict'], 'PASS')

    def test_open_failure_fails(self):
        records = [GOOD[0] + ' open_status=-3'] + GOOD[1:]
        self.assertEqual(self.verdict(records)['verdict'], 'FAIL')

    def test_explicit_result_fail_is_fail_even_when_incomplete(self):
        records = [GOOD[0], 'M5B_RESULT_V1 result=FAIL reason=open status=-3']
        self.assertEqual(self.verdict(records)['verdict'], 'FAIL')

    def test_open_status_zero_passes(self):
        records = [GOOD[0] + ' open_status=0'] + GOOD[1:]
        self.assertEqual(self.verdict(records)['verdict'], 'PASS')

    def test_missing_open_status_is_not_a_pass(self):
        self.assertNotEqual(self.verdict([GOOD[0].replace(' open_status=0', '')] + GOOD[1:])['verdict'], 'PASS')


if __name__ == '__main__':
    unittest.main()
