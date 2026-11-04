#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Parser, classification and cycle loop of replace_capture.py on canned text and a scripted board."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replace_capture as rc


def L(text):
    return f'{text} *{rc.checksum(text):02X}'


def feed(lines):
    state = {}
    for line in lines:
        rc.parse_line(L(line) if line.startswith(('BOOT', 'CLASS', 'GEN')) else line, state)
    return state


CLASS = 'CLASS run=1 cfg={} cfg_gen={} cmp={} cmp_gen={}'


class Parse(unittest.TestCase):
    def test_the_checksum_matches_the_firmware_vectors(self):
        self.assertEqual(rc.checksum('ACK 1'), 0x3A)  # the vectors pinned by the firmware tests
        self.assertEqual(rc.checksum(''), 0)

    def test_gen_lines_keep_the_highest_generation_and_a_new_boot_fixes_the_previous_run(self):
        state = feed(['BOOT 2', 'GEN 5', 'GEN 6', 'GEN 7', 'BOOT 3'])
        self.assertEqual((state['prev_run_boot'], state['prev_run_gen']), (2, 7))
        self.assertEqual(state['last_gen'], 0)

    def test_a_repeated_boot_line_changes_nothing(self):
        state = {}
        rc.parse_line(L('BOOT 2'), state)
        rc.parse_line(L('GEN 4'), state)
        self.assertEqual(rc.parse_line(L('BOOT 2'), state), 'boot_repeat')
        self.assertEqual(state['last_gen'], 4)

    def test_lines_with_a_wrong_or_missing_checksum_or_garbage_before_a_token_are_dropped(self):
        for line in (L('GEN 7')[:-2] + '00', 'GEN 73166', '�GEN 999', 'xx BOOT 9', '~CLASS run=1'):
            state = {}
            self.assertEqual(rc.parse_line(line, state), 'corrupt', line)
            self.assertNotIn('last_gen', state)

    def test_class_lines_are_parsed(self):
        state = feed(['BOOT 2', CLASS.format('valid', 9, 'bad', 10)])
        self.assertEqual(state['class'], {'run': 1, 'cfg': 'valid', 'cfg_gen': 9, 'cmp': 'bad',
                                          'cmp_gen': 10})


class Judge(unittest.TestCase):
    def judged(self, last_gen, cfg, cfg_gen, cmp_state='none', cmp_gen=0):
        cls = {'run': 1, 'cfg': cfg, 'cfg_gen': cfg_gen, 'cmp': cmp_state, 'cmp_gen': cmp_gen}
        return rc.judge(last_gen, cls)['outcome']

    def test_a_valid_file_at_or_after_the_last_printed_generation_is_ok(self):
        self.assertEqual(self.judged(10, 'valid', 10), 'ok')
        self.assertEqual(self.judged(10, 'valid', 11), 'ok')
        self.assertEqual(self.judged(10, 'valid', 40), 'ok', 'the console lags the writes')

    def test_a_valid_file_older_than_a_printed_generation_is_stale(self):
        self.assertEqual(self.judged(10, 'valid', 9), 'stale')

    def test_a_missing_settings_file_is_the_unlink_to_rename_window(self):
        self.assertEqual(self.judged(10, 'none', 0, 'valid', 11), 'missing_cmp_valid')
        self.assertEqual(self.judged(10, 'none', 0, 'bad', 11), 'missing_cmp_bad')
        self.assertEqual(self.judged(10, 'none', 0), 'missing_no_cmp')

    def test_a_bad_settings_file_is_reported(self):
        self.assertEqual(self.judged(10, 'bad', 10), 'cfg_bad')

    def test_pair_refuses_to_judge_a_run_whose_gens_were_not_seen(self):
        state = feed(['BOOT 2', 'GEN 1', 'BOOT 4', 'CLASS run=3 cfg=valid cfg_gen=9 cmp=none cmp_gen=0'])
        self.assertEqual(rc.pair(state)['outcome'], 'previous_run_not_observed')
        self.assertIsNone(rc.pair(feed(['BOOT 5', CLASS.format('valid', 1, 'none', 0)])))

    def test_summary_counts_outcomes(self):
        results = [{'outcome': 'ok'}, {'outcome': 'ok'}, {'outcome': 'missing_cmp_valid'},
                   {'outcome': 'stale'}, {'outcome': 'previous_run_not_observed'}]
        s = rc.summarize(results)
        self.assertEqual((s['judged'], s['outcomes']['ok'], s['outcomes']['missing_cmp_valid'],
                          s['outcomes']['stale'], s['previous_run_not_observed']), (4, 2, 1, 1, 1))


class LoopOnFakeBoard(unittest.TestCase):
    def test_cycles_are_paired_with_the_next_boots_classification(self):
        import argparse
        import pty
        import tempfile
        import threading
        import time

        master, slave = pty.openpty()
        directory = tempfile.mkdtemp()
        flag = os.path.join(directory, 'cut')
        stop = threading.Event()

        def board():
            boot, gen = 1, 0
            os.write(master, (L('BOOT 1') + '\r\n').encode())
            while not stop.is_set():
                if os.path.exists(flag):
                    os.remove(flag)
                    boot += 1
                    # every second cut lands in the window: cfg missing, a valid temporary
                    lines = [L(f'BOOT {boot}')]
                    if boot % 2 == 0:
                        lines.append(L(CLASS.format('none', 0, 'valid', gen + 1).replace('run=1', f'run={boot - 1}')))
                    else:
                        lines.append(L(CLASS.format('valid', gen, 'none', 0).replace('run=1', f'run={boot - 1}')))
                    os.write(master, ''.join(x + '\r\n' for x in lines).encode())
                    gen = 0
                gen += 1
                os.write(master, (L(f'GEN {gen}') + '\r\n').encode())
                time.sleep(0.02)

        thread = threading.Thread(target=board, daemon=True)
        thread.start()
        args = argparse.Namespace(
            port=os.ttyname(slave), baud=115200, cycles=4, cycle_cmd=f'touch {flag}', cut_cmd=None,
            restore_cmd=None, off_seconds=0.1, min_run=0.3, max_run=0.4, boot_timeout=3.0, seed=1,
            log=os.path.join(directory, 'console.log'), out=None)
        try:
            results = rc.run_cycles(args)
        finally:
            stop.set()
            thread.join(timeout=2)
        judged = [r for r in results if 'outcome' in r and r['outcome'] != 'previous_run_not_observed']
        self.assertGreaterEqual(len(judged), 3, results)
        self.assertEqual({r['outcome'] for r in judged}, {'ok', 'missing_cmp_valid'})
        self.assertEqual(rc.summarize(results)['outcomes']['missing_cmp_valid'],
                         sum(1 for r in judged if r['outcome'] == 'missing_cmp_valid'))


if __name__ == '__main__':
    unittest.main()
