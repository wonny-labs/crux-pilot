#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Parser and pairing logic of interrupt_capture.py on canned console text; no hardware."""
import unittest

import interrupt_capture as ic


def L(text):
    """A console line as the image prints it: the text and its checksum."""
    return f'{text} *{ic.checksum(text):02X}'


def feed(lines):
    state = {}
    for line in lines:
        ic.parse_line(L(line) if line.startswith(('ACK', 'BOOT', 'RECOVER')) else line, state)
    return state


class Parse(unittest.TestCase):
    def test_boot_resets_the_ack_counter_and_recover_is_parsed(self):
        state = feed(['ACK 7', 'BOOT 2', 'RECOVER run=1 present=1 last_valid=6 torn=13 damaged=0'])
        self.assertEqual(state['boot'], 2)
        self.assertEqual(state['last_ack'], 0)
        self.assertEqual(state['recover'], {'run': 1, 'present': True, 'last_valid': 6, 'torn': 13,
                                            'damaged': False})

    def test_a_line_with_a_wrong_checksum_is_dropped(self):
        state = {}
        self.assertEqual(ic.parse_line(L('ACK 7')[:-2] + '00', state), 'corrupt')
        self.assertEqual(ic.parse_line('ACK 73166', state), 'corrupt', 'a line without a checksum')
        self.assertEqual(ic.parse_line(L('ACK 8'), state), 'ack')
        self.assertEqual(state['last_ack'], 8, 'a corrupted line must not move the ACK counter')

    def test_garbage_before_a_token_cannot_bypass_the_checksum(self):
        for line in ('\ufffdACK 999', 'xx ACK 1000', '\x00ACK 77777', 'noise BOOT 9', '~RECOVER run=1'):
            state = {}
            self.assertEqual(ic.parse_line(line, state), 'corrupt', line)
            self.assertNotIn('last_ack', state)

    def test_a_garbled_digit_string_cannot_inflate_the_ack_count(self):
        good = L('ACK 7675')
        garbled = good[:-3] + '31 *7F'  # digits appended while power collapsed
        state = {}
        ic.parse_line(good, state)
        ic.parse_line(garbled, state)
        self.assertEqual(state['last_ack'], 7675)

    def test_ack_keeps_the_highest_sequence_and_ignores_noise(self):
        state = feed(['*** Booting Zephyr ***', 'ACK 3', 'ACK 2', 'garbage\x00', 'ACK 9'])
        self.assertEqual(state['last_ack'], 9)

    def test_the_previous_runs_last_ack_is_fixed_when_the_next_boot_line_arrives(self):
        state = feed(['BOOT 1', 'ACK 1', 'ACK 2', 'ACK 3', 'BOOT 2'])
        self.assertEqual(state['prev_run_ack'], 3, 'in-flight ACK lines precede the BOOT line')
        self.assertEqual(state['last_ack'], 0)

    def test_there_is_no_previous_run_before_the_first_boot_line(self):
        self.assertIsNone(feed(['BOOT 1'])['prev_run_ack'])

    def test_a_repeated_boot_line_of_the_same_boot_changes_nothing(self):
        state = feed(['BOOT 2', 'RECOVER run=1 present=1 last_valid=6 torn=0 damaged=0', 'ACK 1', 'ACK 2',
                      'BOOT 2', 'RECOVER run=1 present=1 last_valid=6 torn=0 damaged=0', 'ACK 3'])
        self.assertEqual(state['boot'], 2)
        self.assertEqual(state['last_ack'], 3, 'a repeat must not reset the ACK counter')
        again = {}
        ic.parse_line(L('BOOT 2'), again)
        self.assertEqual(ic.parse_line(L('BOOT 2'), again), 'boot_repeat')

    def test_the_boot_before_a_new_boot_is_remembered_for_pairing(self):
        state = feed(['BOOT 2', 'ACK 1', 'ACK 2', 'BOOT 3'])
        self.assertEqual((state['prev_run_boot'], state['prev_run_ack']), (2, 2))

    def test_start_status_is_recorded(self):
        self.assertEqual(feed(['sd_storage_start: 2'])['start_status'], 2)


class ChecksumVectors(unittest.TestCase):
    """The same fixed vectors as tests/native/file_sd_interrupt (test_g): a change to the checksum on
    either side without the other breaks one of the two suites."""

    def test_the_checksum_matches_the_firmware_vectors(self):
        self.assertEqual(ic.checksum('ACK 1'), 0x3A)
        self.assertEqual(ic.checksum('ACK 7675'), 0x54)
        self.assertEqual(ic.checksum('BOOT 3'), 0x85)
        self.assertEqual(ic.checksum('RECOVER run=2 present=1 last_valid=4 torn=13 damaged=0'), 0xE7)
        self.assertEqual(ic.checksum(''), 0)


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


class Pair(unittest.TestCase):
    RECOVER = 'RECOVER run=2 present=1 last_valid=4 torn=13 damaged=0'

    def test_a_report_is_judged_only_against_the_run_it_describes(self):
        state = feed(['BOOT 2', 'ACK 1', 'ACK 5', 'BOOT 3', self.RECOVER])
        self.assertEqual(ic.pair(state)['acked_lost'], 1)

    def test_a_run_whose_acks_were_never_seen_is_not_judged(self):
        state = feed(['BOOT 2', 'ACK 1', 'BOOT 4', 'RECOVER run=3 present=1 last_valid=4 torn=0 damaged=0'])
        entry = ic.pair(state)
        self.assertEqual(entry['outcome'], 'previous_run_not_observed')
        self.assertNotIn('acked_lost', entry)

    def test_the_first_boot_seen_has_nothing_to_pair(self):
        self.assertIsNone(ic.pair(feed(['BOOT 5', self.RECOVER])))


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
            os.write(master, ('sd_storage_start: 0\r\n' + L('BOOT 1') + '\r\n').encode())
            while not stop.is_set():
                if os.path.exists(flag):
                    os.remove(flag)
                    boot += 1
                    os.write(master, ('sd_storage_start: 0\r\n' + L(f'BOOT {boot}') + '\r\n' +
                                      L(f'RECOVER run={boot - 1} present=1 last_valid={seq - 1} '
                                        f'torn=13 damaged=0') + '\r\n').encode())
                    seq = 0
                seq += 1
                os.write(master, (L(f'ACK {seq}') + '\r\n').encode())
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


class ObserveWithPortLoss(unittest.TestCase):
    """--observe on a port that vanishes and returns, as the OpenSDA port does when power is cut."""

    def test_reports_are_paired_across_a_vanished_port_and_unseen_boots_are_not_judged(self):
        import argparse
        import os
        import pty
        import tempfile
        import threading
        import time

        directory = tempfile.mkdtemp()
        link = os.path.join(directory, 'port')
        master_a, slave_a = pty.openpty()
        os.symlink(os.ttyname(slave_a), link)

        def feeder():
            # boot 1 is seen with ACKs; the cut and boot 2 happen while the port is gone and its
            # repeated report arrives only after boot 3 has started: run2 was never observed.
            os.write(master_a, ''.join(L(x) + '\r\n' for x in ('BOOT 1', 'ACK 1', 'ACK 2', 'ACK 3')).encode())
            time.sleep(0.4)
            os.close(master_a)  # the port vanishes
            os.remove(link)
            time.sleep(0.5)
            master_b, slave_b = pty.openpty()
            os.symlink(os.ttyname(slave_b), link)  # the port returns
            report = 'RECOVER run={} present=1 last_valid={} torn=13 damaged=0'
            os.write(master_b, ''.join(L(x) + '\r\n' for x in ('BOOT 2', report.format(1, 2), 'ACK 1', 'ACK 2')).encode())
            time.sleep(0.3)
            os.write(master_b, ''.join(L(x) + '\r\n' for x in ('BOOT 3', report.format(2, 1), 'ACK 1')).encode())
            time.sleep(0.6)

        thread = threading.Thread(target=feeder, daemon=True)
        thread.start()
        args = argparse.Namespace(port=link, baud=115200, duration=3.0, log=os.path.join(directory, 'c.log'))
        results = ic.run_observe(args)
        thread.join(timeout=3)
        judged = [r for r in results if 'acked_lost' in r]
        self.assertEqual(len(judged), 2, results)
        self.assertEqual((judged[0]['boot'], judged[0]['last_ack'], judged[0]['last_valid'],
                          judged[0]['acked_lost']), (2, 3, 2, 1))
        self.assertEqual((judged[1]['boot'], judged[1]['last_ack'], judged[1]['last_valid'],
                          judged[1]['acked_lost']), (3, 2, 1, 1))
        s = ic.summarize(results)
        self.assertEqual((s['judged'], s['with_acked_lost']), (2, 2))


class PortNotThereYet(unittest.TestCase):
    def test_a_port_that_appears_late_is_waited_for_not_an_error(self):
        import os
        import pty
        import tempfile
        import threading
        import time

        directory = tempfile.mkdtemp()
        link = os.path.join(directory, 'port')
        console = ic.Console(link, 115200)  # the port does not exist yet: must not raise
        master, slave = pty.openpty()

        def appear():
            time.sleep(0.5)
            os.symlink(os.ttyname(slave), link)
            time.sleep(0.2)
            os.write(master, (L('ACK 1') + '\r\n').encode())

        threading.Thread(target=appear, daemon=True).start()
        lines = list(console.lines(2.0))
        self.assertIn(L('ACK 1'), lines)


if __name__ == '__main__':
    unittest.main()
