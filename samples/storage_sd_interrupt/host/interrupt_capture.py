#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pair console ACK lines with the next boot's recovery report across interruptions.

The image prints, at 115200 baud:  sd_storage_start: <status> / BOOT <n> /
RECOVER run=<n-1> present=<0|1> last_valid=<k> torn=<bytes> damaged=<0|1> / ACK <seq>.
ACK <seq> is printed only after record <seq> was synced. For every interruption this records the last ACK
seen and what the next boot found. Outcomes are recorded, not asserted.

Interruption methods (the script never decides how power is cut):
  --cycle-cmd CMD            one command that interrupts and restarts the board (a probe reset);
                             this restarts the CPU only and is NOT power-loss evidence
  --cut-cmd CMD --restore-cmd CMD   remove power, wait --off-seconds, restore it
"""
import argparse
import json
import os
import random
import re
import select
import subprocess
import sys
import termios
import time

RECOVER = re.compile(r'RECOVER run=(\d+) present=([01]) last_valid=(\d+) torn=(\d+) damaged=([01])')
BOOT = re.compile(r'BOOT (\d+)')
ACK = re.compile(r'ACK (\d+)')
START = re.compile(r'sd_storage_start: (-?\d+)')


def parse_line(line, state):
    """Update state from one console line; return the event name or None."""
    if (m := START.search(line)):
        state['start_status'] = int(m.group(1))
        return 'start'
    if (m := BOOT.search(line)):
        # The console is an ordered stream: every ACK the previous run printed precedes this line.
        state['prev_run_ack'] = state.get('last_ack')
        state['boot'] = int(m.group(1))
        state['last_ack'] = 0
        state['recover'] = None
        return 'boot'
    if (m := RECOVER.search(line)):
        state['recover'] = {'run': int(m.group(1)), 'present': m.group(2) == '1',
                            'last_valid': int(m.group(3)), 'torn': int(m.group(4)),
                            'damaged': m.group(5) == '1'}
        return 'recover'
    if (m := ACK.search(line)):
        state['last_ack'] = max(state.get('last_ack', 0), int(m.group(1)))
        return 'ack'
    return None


def judge(last_ack, recover):
    """Compare the previous run's last acknowledged record with what the next boot recovered."""
    last_valid = recover['last_valid']
    return {'last_ack': last_ack, 'last_valid': last_valid, 'torn_bytes': recover['torn'],
            'damaged_record': recover['damaged'], 'run_present': recover['present'],
            'acked_lost': max(0, last_ack - last_valid),
            'unacked_present': max(0, last_valid - last_ack)}


class Console:
    def __init__(self, path, rate):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = getattr(termios, f'B{rate}')
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.buffer = b''

    def lines(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if select.select([self.fd], [], [], 0.1)[0]:
                self.buffer += os.read(self.fd, 4096)
            while b'\n' in self.buffer:
                line, self.buffer = self.buffer.split(b'\n', 1)
                yield line.decode('ascii', 'replace').strip()


def run_cycles(args):
    console = Console(args.port, args.baud)
    rng = random.Random(args.seed)
    state = {}
    results = []
    log = open(args.log, 'w')

    def pump(seconds, until=None):
        for line in console.lines(seconds):
            log.write(line + '\n')
            log.flush()
            event = parse_line(line, state)
            if until and event == until:
                return True
        return False

    def restart():
        if args.cycle_cmd:
            subprocess.run(args.cycle_cmd, shell=True, check=True)
        else:
            subprocess.run(args.cut_cmd, shell=True, check=True)
            pump(args.off_seconds)
            subprocess.run(args.restore_cmd, shell=True, check=True)

    pump(0.5)
    restart()
    for cycle in range(1, args.cycles + 1):
        if not pump(args.boot_timeout, until='recover') and state.get('boot') != 1:
            results.append({'cycle': cycle, 'outcome': 'no_recovery_report',
                            'start_status': state.get('start_status')})
            print(json.dumps(results[-1]), flush=True)
            restart()
            continue
        boot_recover = state.get('recover')
        previous_ack = state.get('prev_run_ack')
        if boot_recover is not None and previous_ack is not None:
            entry = {'cycle': cycle, 'boot': state['boot'], 'start_status': state.get('start_status')}
            entry.update(judge(previous_ack, boot_recover))
            results.append(entry)
            print(json.dumps(entry), flush=True)
        pump(rng.uniform(args.min_run, args.max_run))
        restart()
    log.close()
    return results


def summarize(results):
    judged = [r for r in results if 'acked_lost' in r]
    return {'cycles': len(results), 'judged': len(judged),
            'no_report': sum(1 for r in results if r.get('outcome') == 'no_recovery_report'),
            'with_acked_lost': sum(1 for r in judged if r['acked_lost'] > 0),
            'with_torn_tail': sum(1 for r in judged if r['torn_bytes'] > 0),
            'with_damaged_record': sum(1 for r in judged if r['damaged_record'])}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--cycles', type=int, default=10)
    parser.add_argument('--cycle-cmd')
    parser.add_argument('--cut-cmd')
    parser.add_argument('--restore-cmd')
    parser.add_argument('--off-seconds', type=float, default=3.0)
    parser.add_argument('--min-run', type=float, default=1.0)
    parser.add_argument('--max-run', type=float, default=6.0)
    parser.add_argument('--boot-timeout', type=float, default=25.0)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--log', default='interrupt-console.log')
    parser.add_argument('--out', default='interrupt-results.json')
    args = parser.parse_args()
    if not args.cycle_cmd and not (args.cut_cmd and args.restore_cmd):
        parser.error('give --cycle-cmd, or both --cut-cmd and --restore-cmd')
    results = run_cycles(args)
    summary = summarize(results)
    with open(args.out, 'w') as f:
        json.dump({'summary': summary, 'cycles': results}, f, indent=2)
    print(json.dumps(summary))
    return 0


if __name__ == '__main__':
    sys.exit(main())
