#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pair the last generation the replace-pattern image printed with what the next boot found.

The image (samples/storage_sd_replace) rewrites "cfg" through "cfg.cmp" and fs_rename in a loop and
prints, at 115200 baud, BOOT <n>, CLASS run=<n-1> cfg=<none|valid|bad> cfg_gen=<g> cmp=<none|valid|bad>
cmp_gen=<g> and GEN <g> (after the rename of generation g returned), each with a checksum " *XX".

For every interruption this records one outcome (outcomes are recorded, not asserted):
  ok                  cfg is valid at the last printed generation or later (the console lags the writes)
  stale               cfg is valid but older than a generation the board had printed
  missing_cmp_valid   cfg is gone and cfg.cmp is valid: the cut fell between the unlink and the rename
  missing_cmp_bad     cfg is gone and cfg.cmp is incomplete
  missing_no_cmp      neither file exists
  cfg_bad             cfg exists but is incomplete or damaged
  previous_run_not_observed   the generations of that run never reached this host: not judged

The script never decides how power is cut: give --cycle-cmd (a probe reset, which restarts the CPU only)
or --cut-cmd with --restore-cmd (power).
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                                'storage_sd_interrupt', 'host'))
from interrupt_capture import Console, checksum  # noqa: E402  (shared console and line checksum)

TOKEN = re.compile(r'(BOOT|CLASS|GEN)\b')
CHECKED = re.compile(r'^(.*\S) \*([0-9A-F]{2})$')
START = re.compile(r'sd_storage_start: (-?\d+)')
BOOT = re.compile(r'^BOOT (\d+)$')
GEN = re.compile(r'^GEN (\d+)$')
CLASS = re.compile(r'^CLASS run=(\d+) cfg=(none|valid|bad) cfg_gen=(\d+) cmp=(none|valid|bad) cmp_gen=(\d+)$')


def parse_line(line, state):
    """Update state from one console line and return the event name or None.

    BOOT, CLASS and GEN lines carry a checksum. Any line containing one of those words (garbage may
    precede it) without a valid checksum is dropped as corrupt: power collapsing mid-line can garble
    digits and must not move the generation counter."""
    if TOKEN.search(line) or ' *' in line:
        m = CHECKED.match(line.strip())
        if not m or checksum(m.group(1)) != int(m.group(2), 16):
            return 'corrupt'
        line = m.group(1)
    if (m := START.search(line)):
        state['start_status'] = int(m.group(1))
        return 'start'
    if (m := BOOT.match(line)):
        boot = int(m.group(1))
        if boot == state.get('boot'):
            return 'boot_repeat'  # the image repeats its report: the port may have been absent
        state['prev_run_boot'] = state.get('boot')
        state['prev_run_gen'] = state.get('last_gen')
        state['boot'] = boot
        state['last_gen'] = 0
        state['class'] = None
        state['judged'] = False
        return 'boot'
    if (m := CLASS.match(line)):
        state['class'] = {'run': int(m.group(1)), 'cfg': m.group(2), 'cfg_gen': int(m.group(3)),
                          'cmp': m.group(4), 'cmp_gen': int(m.group(5))}
        return 'class'
    if (m := GEN.match(line)):
        state['last_gen'] = max(state.get('last_gen', 0), int(m.group(1)))
        return 'gen'
    return None


def judge(last_gen, cls):
    """Classify what a cut left, given the last generation printed before it."""
    if cls['cfg'] == 'valid':
        outcome = 'ok' if cls['cfg_gen'] >= last_gen else 'stale'
    elif cls['cfg'] == 'bad':
        outcome = 'cfg_bad'
    elif cls['cmp'] == 'valid':
        outcome = 'missing_cmp_valid'
    elif cls['cmp'] == 'bad':
        outcome = 'missing_cmp_bad'
    else:
        outcome = 'missing_no_cmp'
    return {'outcome': outcome, 'last_gen': last_gen, 'cfg': cls['cfg'], 'cfg_gen': cls['cfg_gen'],
            'cmp': cls['cmp'], 'cmp_gen': cls['cmp_gen']}


def pair(state):
    """The judged entry for the current boot's CLASS report, or None when there is nothing to pair."""
    cls = state.get('class')
    if cls is None or state.get('prev_run_boot') is None:
        return None
    base = {'boot': state['boot'], 'start_status': state.get('start_status')}
    if cls['run'] != state['prev_run_boot']:
        return base | {'outcome': 'previous_run_not_observed', 'report': cls}
    return base | judge(state['prev_run_gen'], cls)


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
        if not pump(args.boot_timeout, until='class') and state.get('boot') != 1:
            results.append({'cycle': cycle, 'outcome': 'no_report',
                            'start_status': state.get('start_status')})
            print(json.dumps(results[-1]), flush=True)
            restart()
            continue
        entry = pair(state) if not state.get('judged') else None
        if entry is not None:
            state['judged'] = True
            entry = {'cycle': cycle} | entry
            results.append(entry)
            print(json.dumps(entry), flush=True)
        pump(rng.uniform(args.min_run, args.max_run))
        restart()
    log.close()
    return results


def summarize(results):
    judged = [r for r in results if r.get('outcome') not in ('previous_run_not_observed', 'no_report')]
    outcomes = {}
    for r in judged:
        outcomes[r['outcome']] = outcomes.get(r['outcome'], 0) + 1
    return {'cycles': len(results), 'judged': len(judged), 'outcomes': outcomes,
            'previous_run_not_observed': sum(1 for r in results
                                             if r.get('outcome') == 'previous_run_not_observed'),
            'no_report': sum(1 for r in results if r.get('outcome') == 'no_report')}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--cycles', type=int, default=100)
    parser.add_argument('--cycle-cmd')
    parser.add_argument('--cut-cmd')
    parser.add_argument('--restore-cmd')
    parser.add_argument('--off-seconds', type=float, default=3.0)
    parser.add_argument('--min-run', type=float, default=0.5)
    parser.add_argument('--max-run', type=float, default=4.0)
    parser.add_argument('--boot-timeout', type=float, default=25.0)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--log', default='replace-console.log')
    parser.add_argument('--out', default='replace-results.json')
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
