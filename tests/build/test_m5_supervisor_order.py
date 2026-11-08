"""Arrival-order checks of the M5 supervisor against the real UART captures of the EVKB bench (2026-11-08).

The board prints its link events at once and its idle samples as a burst at the end of the phase, and the host
records link rows while a command runs, so a link row can carry a later board time than the first idle sample
and can arrive before or after the command write. The first real run passed phase 1 by the luck of a USB chunk
split; the second failed it (`Resource settling/idle sampling deadline failed`). These tests replay the real
captures and every placement of the command write between the arrivals.
"""
import copy
import json
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m5_network as cap
import m5_network_oracle as oracle

FIXTURES = Path(__file__).resolve().parent / 'fixtures' / 'm5-real-captures'
ABI = {'ETIMEDOUT': 116, 'ECONNRESET': 104, 'ENETDOWN': 115, 'ENETUNREACH': 114}
IDLE = dict(event='IDLE', checkpoint=0, contexts=0, rx_pkt=16, tx_pkt=16, rx_buf=32, tx_buf=32, main_free=3312, observer_free=1784)


def link(token, seq, at, state):
    return dict(event='LINK', token=token, seq=seq, at=at, link=state, admin=1, address=1, ipv4=3232255234)


def replay(run, batch):
    """The real chunks of `run` through the real supervisor until phase 1 is validated and phase 2 is prepared.

    `batch` 0 delivers the lines of one receive chunk together (what the adapter's poll does); a larger value
    also groups chunks that arrive within that many seconds of the first one of a batch.
    """
    directory = FIXTURES / run
    chunks = [json.loads(line) for line in (directory / 'receive.jsonl').read_text().splitlines()]
    writes = [json.loads(line) for line in (directory / 'writes.jsonl').read_text().splitlines()]
    operations = [json.loads(line) for line in (directory / 'operations.jsonl').read_text().splitlines()]
    pending, events = b'', []
    for chunk in chunks:
        pending += bytes.fromhex(chunk['hex'])
        while b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
            line = line.rstrip(b'\r').decode()
            if line.startswith('M5_BOOT'):
                events.append(dict(kind='boot', monotonic=chunk['monotonic'], record=dict(source=line.split('=')[1])))
            elif line.startswith('M5 '):
                events.append(dict(kind='target', monotonic=chunk['monotonic'], record=oracle.parse_record(line)))
    batches = []
    for event in events:
        if batches and (event['monotonic'] == batches[-1][0]['monotonic'] or event['monotonic'] - batches[-1][0]['monotonic'] < batch):
            batches[-1].append(event)
        else:
            batches.append([event])
    start = next(o['monotonic'] for o in operations if o['event'] == 'approved_reset_completed')
    supervisor = cap.Supervisor(int(writes[0]['line'].split()[1]), events[0]['record']['source'], start, ABI, through_phase=6)
    written, last = iter(writes), start
    for group in batches:
        now = max(group[-1]['monotonic'], last) + .001
        last = now
        for event in group:
            if event['kind'] == 'boot':
                supervisor.boot(event['record']['source'], now)
                next(written)
            else:
                supervisor.target(event['record'], now, received=event['monotonic'])
        if supervisor.state == 'prepared':
            if cap.m5_series.PHASES[supervisor.sequence - 1][1]:
                return supervisor
            line = supervisor.next_command(now)
            write = next(written)
            assert write['line'] == line
            last = max(now, write['monotonic']) + .001          # the guard check inside the write takes about a second
            supervisor.command_written(write['monotonic'], last)
    raise AssertionError('phase 1 did not complete')


class RealCaptures(unittest.TestCase):
    def test_both_real_boots_pass_phase_1_for_every_batching(self):
        for run in ('run4', 'run5'):
            for batch in (0., .02, .25):
                with self.subTest(run=run, batch=batch):
                    supervisor = replay(run, batch)
                    self.assertEqual((supervisor.state, supervisor.sequence, supervisor.aborted), ('prepared', 2, False))


class Phase1Arrivals(unittest.TestCase):
    """The command write can fall before, between or after the two link rows that the board prints around its link-up."""
    NUMBERS = {'run4': (1810, 3821, (3038, 4040, 5040), 5075), 'run5': (1816, 3827, (3001, 4002, 5012), 5047)}

    def phase1(self, run, cut):
        down, up, idle, done = self.NUMBERS[run]
        token = 31
        s = cap.Supervisor(token, '123456789abc', 0, ABI)
        s.boot('123456789abc', .1)
        s.target(dict(event='ARM', token=token, at=0), .2)
        s.target(dict(event='READY', token=token, seq=1, ticket=99, at=1800), .3)
        arrivals = [link(token, 0, down, 1), link(token, 1, up, 2)]
        now = .4
        for index, row in enumerate(arrivals):
            if index == cut:
                s.next_command(now);now += 1.2
            s.target(row, now);now += .1
        if cut == len(arrivals):
            s.next_command(now);now += 1.2
        for at in idle:
            s.target(dict(IDLE, token=token, seq=1, at=at), now);now += .01
        s.target(dict(event='DONE', token=token, seq=1, at=done), now)
        return s

    def test_every_placement_of_the_command_write_passes(self):
        for run in self.NUMBERS:
            for cut in (0, 1, 2):
                with self.subTest(run=run, cut=cut):
                    s = self.phase1(run, cut)
                    self.assertEqual((s.state, s.sequence, s.aborted), ('ready', 2, False))

    def test_the_idle_window_is_still_enforced(self):
        token = 31
        for idle, done in (((3001, 4002, 9012), 9047), ((3001, 3500, 5012), 5047), ((3001, 4002, 5012), 5047)):
            s = cap.Supervisor(token, '123456789abc', 0, ABI)
            s.boot('123456789abc', .1)
            s.target(dict(event='ARM', token=token, at=0), .2)
            s.target(dict(event='READY', token=token, seq=1, ticket=99, at=1800), .3)
            s.next_command(.4)
            s.target(link(token, 1, 3827, 2), 1.6)
            for index, at in enumerate(idle):
                s.target(dict(IDLE, token=token, seq=1, at=at), 1.7 + index / 100)
            if idle == (3001, 4002, 5012):
                s.target(dict(event='DONE', token=token, seq=1, at=done), 1.8)
                self.assertEqual(s.state, 'ready')
            else:
                with self.assertRaisesRegex(ValueError, 'idle sampling deadline'):
                    s.target(dict(event='DONE', token=token, seq=1, at=done), 1.8)


class LateLinkRowsOfAnotherPhase(unittest.TestCase):
    def test_a_link_row_of_the_previous_phase_arriving_after_the_command_does_not_abort_a_count_phase(self):
        from test_m5_series import complete, SOURCE
        boot = complete()['boots'][0]
        token = boot['nonce']
        timeline = [(e['monotonic'], 'target', e['record']) for e in boot['uart']]
        timeline += [(e['monotonic'], 'write', e) for e in boot['writes']]
        timeline += [(e['monotonic'], 'peer', e) for rows in boot['peers'].values() for e in rows if e['event'] in ('ready', 'request')]
        timeline += [(e['monotonic'], 'control', e) for e in boot['controls']]
        write2 = next(e for e in boot['writes'] if e['line'].startswith('P %d 2 ' % token))
        late = link(token, 1, 3827, 2)
        timeline.append((write2['monotonic'] + .0001, 'target', late))
        s = cap.Supervisor(token, SOURCE, 0, ABI, through_phase=2)
        arm = None
        for now, kind, row in sorted(timeline, key=lambda e: e[0]):
            if s.state == 'finished':
                break
            if kind == 'target':
                if row['event'] == 'BOOT':
                    arm = s.boot(row['source'], now)
                else:
                    s.target(row, now)
            elif kind == 'write':
                if row['line'].startswith('A '):
                    self.assertEqual(row['line'], arm)
                else:
                    self.assertEqual(row['line'], s.next_command(now))
            elif kind == 'peer' and row['event'] == 'ready':
                if s.peer_phase is not None and s.stopped_phase != s.peer_phase:
                    s.peer_stopped(s.peer_phase, True, now)
                s.peer_ready(row, now)
        self.assertEqual(s.state, 'finished')
        self.assertFalse(s.aborted)


if __name__ == '__main__':
    unittest.main()
