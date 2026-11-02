#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Parser and pairing logic of interrupt_capture.py on canned console text; no hardware."""
import unittest

import interrupt_capture as ic


def feed(lines):
    state = {}
    for line in lines:
        ic.parse_line(line, state)
    return state


class Parse(unittest.TestCase):
    def test_boot_resets_the_ack_counter_and_recover_is_parsed(self):
        state = feed(['ACK 7', 'BOOT 2', 'RECOVER run=1 present=1 last_valid=6 torn=13 damaged=0'])
        self.assertEqual(state['boot'], 2)
        self.assertEqual(state['last_ack'], 0)
        self.assertEqual(state['recover'], {'run': 1, 'present': True, 'last_valid': 6, 'torn': 13,
                                            'damaged': False})

    def test_ack_keeps_the_highest_sequence_and_ignores_noise(self):
        state = feed(['*** Booting Zephyr ***', 'ACK 3', 'ACK 2', 'garbage\x00', 'ACK 9'])
        self.assertEqual(state['last_ack'], 9)

    def test_the_previous_runs_last_ack_is_fixed_when_the_next_boot_line_arrives(self):
        state = feed(['BOOT 1', 'ACK 1', 'ACK 2', 'ACK 3', 'BOOT 2'])
        self.assertEqual(state['prev_run_ack'], 3, 'in-flight ACK lines precede the BOOT line')
        self.assertEqual(state['last_ack'], 0)

    def test_there_is_no_previous_run_before_the_first_boot_line(self):
        self.assertIsNone(feed(['BOOT 1'])['prev_run_ack'])

    def test_start_status_is_recorded(self):
        self.assertEqual(feed(['sd_storage_start: 2'])['start_status'], 2)


class Judge(unittest.TestCase):
    def recover(self, last_valid, torn=0, damaged=False, present=True):
        return {'last_valid': last_valid, 'torn': torn, 'damaged': damaged, 'present': present}

    def test_nothing_lost_when_the_last_ack_was_recovered(self):
        r = ic.judge(10, self.recover(10))
        self.assertEqual((r['acked_lost'], r['unacked_present']), (0, 0))

    def test_acknowledged_records_missing_are_counted(self):
        r = ic.judge(10, self.recover(7, torn=64, damaged=True))
        self.assertEqual(r['acked_lost'], 3)
        self.assertTrue(r['damaged_record'])

    def test_a_record_that_landed_without_its_ack_is_not_a_loss(self):
        r = ic.judge(10, self.recover(11))
        self.assertEqual((r['acked_lost'], r['unacked_present']), (0, 1))


class Summary(unittest.TestCase):
    def test_summary_counts_losses_and_missing_reports(self):
        results = [ic.judge(5, {'last_valid': 5, 'torn': 0, 'damaged': False, 'present': True})
                   | {'cycle': 1},
                   ic.judge(5, {'last_valid': 3, 'torn': 13, 'damaged': False, 'present': True})
                   | {'cycle': 2},
                   {'cycle': 3, 'outcome': 'no_recovery_report'}]
        s = ic.summarize(results)
        self.assertEqual((s['cycles'], s['judged'], s['no_report'], s['with_acked_lost'],
                          s['with_torn_tail']), (3, 2, 1, 1, 1))


if __name__ == '__main__':
    unittest.main()


class LoopOnFakeBoard(unittest.TestCase):
    """run_cycles() end to end on a pseudo-terminal with a scripted board; no hardware."""

    def test_three_interruptions_are_paired_with_the_next_boots_report(self):
        import argparse
        import os
        import pty
        import tempfile
        import threading
        import time

        master, slave = pty.openpty()
        flag_dir = tempfile.mkdtemp()
        flag = os.path.join(flag_dir, 'cut')
        stop = threading.Event()

        def board():
            boot, seq = 1, 0
            os.write(master, b'sd_storage_start: 0\r\nBOOT 1\r\n')
            while not stop.is_set():
                if os.path.exists(flag):
                    os.remove(flag)
                    boot += 1
                    os.write(master, (f'sd_storage_start: 0\r\nBOOT {boot}\r\nRECOVER run={boot - 1} '
                                      f'present=1 last_valid={seq - 1} torn=13 damaged=0\r\n').encode())
                    seq = 0
                seq += 1
                os.write(master, f'ACK {seq}\r\n'.encode())
                time.sleep(0.02)

        thread = threading.Thread(target=board, daemon=True)
        thread.start()
        args = argparse.Namespace(
            port=os.ttyname(slave), baud=115200, cycles=3, cycle_cmd=f'touch {flag}', cut_cmd=None,
            restore_cmd=None, off_seconds=0.1, min_run=0.3, max_run=0.4, boot_timeout=3.0, seed=1,
            log=os.path.join(flag_dir, 'console.log'), out=None)
        try:
            results = ic.run_cycles(args)
        finally:
            stop.set()
            thread.join(timeout=2)
        judged = [r for r in results if 'acked_lost' in r]
        self.assertGreaterEqual(len(judged), 2, results)
        for r in judged:
            self.assertEqual(r['acked_lost'], 1, 'the scripted board always loses exactly the last ACK')
            self.assertEqual(r['torn_bytes'], 13)
        self.assertEqual(ic.summarize(results)['with_acked_lost'], len(judged))
