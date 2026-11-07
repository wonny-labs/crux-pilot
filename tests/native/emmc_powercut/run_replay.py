#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Power-cut replay on the eMMC simulator.

Each workload runs once against the simulator from one provisioned baseline and records a trace.
For each chosen cut position and model, the medium is rebuilt from the baseline and the trace, a
new process mounts it without formatting and checks the invariants, and the result is classified:

  I1 mount  the volume mounts            I4 untouched  files/keep is byte-exact
  I2 data   acknowledged steps hold      I5 recommit   a write, sync and remount succeed
  I3 replace  settings/w2 is one complete generation, not older than acknowledged
  read_error  a file that should exist could not be read, as distinct from a stale value

On FatFs every result is recorded as an observation and only the controls are required, because
FatFs does not claim these invariants. Every result is an observation of the simulator's model,
not evidence about power loss on a device.

Cut positions: on the cached profile, each side of every flush with the lost, prefix and one
reordered draw; then --samples positions per workload and model, drawn by seed, from write
positions only for torn, because only a write at the cut can tear. The no-cache profile has no
flush, so only prefix and torn apply. The sample count alone selects positions,
never the clock; --max-seconds fails a run that takes longer and never skips work. A rebuilt image
that was already verified for the same workload and acknowledged count is not verified again,
because the verifier is deterministic; the JSON counts cases and processes separately. The
verifier runs without a cache, so it reads the rebuilt image as it is.

A W1 case that fails data is verified again with --acked-only, which asks only that every
acknowledged record is intact. The JSON then counts it as I2_acknowledged_intact, when only a
later, unacknowledged record is stale, or as I2_acknowledged_lost. A process that runs longer than
TIMEOUT seconds is killed and recorded as a failure, with its seed, position and image.
"""
import argparse
import hashlib
import json
import os
import platform
import random
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from replay_model import (SECTOR, acked_at, build_image,  # noqa: E402
                          mandatory_positions, parse_trace)

MEDIUM = 8192 * SECTOR
WORKLOADS = ('W1', 'W2', 'W3')
PROFILES = {
    'cached': [],  # the simulator's default cache size
    'nocache': ['--emmc-cache-kb=0'],
}
PROFILE_MODELS = {
    'cached': ('lost', 'prefix', 'reordered', 'torn'),
    'nocache': ('prefix', 'torn'),
}
FLUSH_MODELS = ('lost', 'prefix', 'reordered')
METAROOT_MODELS = ('prefix', 'torn', 'reordered')
FIELDS = ('mount', 'data', 'read_error', 'replace', 'untouched', 'recommit')
VIOLATIONS = {'mount': 'I1', 'data': 'I2', 'replace': 'I3', 'untouched': 'I4', 'recommit': 'I5'}
LINE = re.compile(r'POWERCUT ' + ' '.join(f'{field}=([01])' for field in FIELDS))
TIMEOUT = 60


def derive(*parts):
    """A seed for one draw, stable across runs and Python versions."""
    text = ':'.join(str(part) for part in parts).encode()
    return int.from_bytes(hashlib.sha256(text).digest()[:8], 'little')


def violations(result):
    found = [invariant for field, invariant in VIOLATIONS.items() if result[field] == 0]
    if result['read_error']:
        found.append('read_error')
    return found


class RunError(RuntimeError):
    pass


class Replay:
    def __init__(self, args):
        self.args = args
        self.exes = {'fix': args.exe_fix, 'nofix': args.exe_nofix}
        self.work = os.path.join(args.out, f'work-{args.fs}')
        self.kept = os.path.join(args.out, f'failed-{args.fs}')
        self.image = os.path.join(self.work, 'medium.bin')
        self.memo = {}
        self.cases = 0
        self.processes = 0
        self.failures = []

    def run(self, exe, *options):
        """Returns (exit code, stdout lines, stderr); the code is None for a process that hung."""
        try:
            proc = subprocess.run([self.exes[exe], *options], capture_output=True, text=True,
                                  timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            return None, [], f'killed after {TIMEOUT} s'
        return proc.returncode, proc.stdout.splitlines(), proc.stderr

    def provision(self):
        os.makedirs(self.work, exist_ok=True)
        shutil.rmtree(self.kept, ignore_errors=True)  # images of an earlier run's failures
        path = os.path.join(self.work, 'baseline.bin')
        if os.path.exists(path):
            os.remove(path)  # the simulator loads an existing image, and this one must be new
        rc, out, err = self.run('fix', f'--emmc-image={path}', '--emmc-cache-kb=0',
                                '--phase=provision')
        if rc != 0 or out != ['PROVISIONED']:
            raise RunError(f'provision failed: {rc} {out} {err}')
        with open(path, 'rb') as handle:
            self.baseline = handle.read()
        if len(self.baseline) != MEDIUM:
            raise RunError(f'the baseline has {len(self.baseline)} bytes')

    def trace(self, exe, workload, profile, phase='run'):
        run_image = os.path.join(self.work, 'run.bin')
        path = os.path.join(self.work, f'{exe}-{workload}-{profile}-{phase}.trace')
        shutil.copyfile(os.path.join(self.work, 'baseline.bin'), run_image)
        rc, out, err = self.run(exe, f'--emmc-image={run_image}', f'--emmc-trace={path}',
                                *PROFILES[profile], f'--phase={phase}', f'--workload={workload}')
        if rc != 0 or out != ['RAN']:
            raise RunError(f'{exe} {workload} {profile} {phase} failed: {rc} {out} {err}')
        with open(path, 'rb') as handle:
            return parse_trace(handle.read())

    def verify(self, exe, image, workload, acked, acked_only=False):
        """Returns the six fields and None, or None and why the verifier gave no POWERCUT line.

        acked_only narrows the W1 data check to the acknowledged records; such a call is a second
        look at a case, not a case of its own.
        """
        if not acked_only:
            self.cases += 1
        key = (exe, workload, acked, acked_only, hashlib.sha256(image).digest())
        if key in self.memo:
            return self.memo[key]
        with open(self.image, 'wb') as handle:
            handle.write(image)
        rc, out, err = self.run(exe, f'--emmc-image={self.image}', '--emmc-cache-kb=0',
                                '--phase=verify', f'--workload={workload}', f'--acked={acked}',
                                *(['--acked-only'] if acked_only else []))
        self.processes += 1
        lines = [LINE.fullmatch(line) for line in out]
        if rc != 0 or len(out) != 1 or lines[0] is None:
            answer = (None, f'rc {rc} {out} {err[-500:]}')
            print(f'verifier gave no result: {answer[1]}', file=sys.stderr)
        else:
            answer = ({field: int(value) for field, value in zip(FIELDS, lines[0].groups())},
                      None)
        self.memo[key] = answer
        return answer

    def keep(self, name, image):
        os.makedirs(self.kept, exist_ok=True)
        path = os.path.join(self.kept, f'{name}.bin')
        with open(path, 'wb') as handle:
            handle.write(image)
        return os.path.relpath(path, self.args.out)

    def fail(self, case, reason, image):
        case = dict(case, reason=reason)
        case['image'] = self.keep(f'case-{len(self.failures)}', image)
        self.failures.append(case)
        print(f'FAIL {reason}: {json.dumps(case, sort_keys=True)}')

    def case(self, exe, events, workload, profile, model, position):
        seed = derive(self.args.seed, exe, workload, profile, model, position)
        image = build_image(self.baseline, events, position, model, seed)
        acked = acked_at(events, position)
        record = {'exe': exe, 'workload': workload, 'profile': profile, 'model': model,
                  'position': position, 'seed': seed, 'acked': acked}
        result, reason = self.verify(exe, image, workload, acked)
        if result is None:
            record['no_result'] = reason
        return record, image, result

    def positions(self, events, workload, profile):
        """Distinct (position, model) pairs: the mandatory ones first, then the sampled ones."""
        chosen = []
        if profile == 'cached':
            for position in mandatory_positions(events):
                chosen += [(position, model) for model in FLUSH_MODELS]
        models = METAROOT_MODELS if profile == 'cached' else PROFILE_MODELS[profile]
        for position in self.metaroot_positions(events):
            chosen += [(position, model) for model in models]
        writes = [index for index, event in enumerate(events) if event.kind == 'write']
        for model in PROFILE_MODELS[profile]:
            draw = random.Random(derive(self.args.seed, workload, profile, model, 'positions'))
            # torn tears the write at the cut, so only a write position tears anything
            population = writes if model == 'torn' else range(len(events) + 1)
            sample = draw.sample(population, min(self.args.samples, len(population)))
            chosen += [(position, model) for position in sorted(sample)]
        return list(dict.fromkeys(chosen))  # a drawn position can also be a mandatory one

    def metaroot_positions(self, events):
        """Each side of every metaroot sector write. FatFs has none; a later task adds them."""
        metaroot = frozenset()
        return sorted({position for index, event in enumerate(events)
                       if event.kind == 'write' and event.sector in metaroot
                       for position in (index, index + 1)})

    def sanity(self, events, workload, profile, report):
        """The verifier passes on the baseline and on the complete run."""
        for position in (0, len(events)):
            record, image, result = self.case('fix', events, workload, profile, 'prefix',
                                              position)
            ok = result is not None and not violations(result)
            report.append(dict(record, result=result, passed=ok))
            if not ok:
                self.fail(record, 'the verifier does not pass on an uncut image', image)

    def observe(self, events, workload, profile):
        groups = {}
        failed = []
        for position, model in self.positions(events, workload, profile):
            record, image, result = self.case('fix', events, workload, profile, model, position)
            group = groups.setdefault(model, {'cases': 0, 'passed': 0, 'no_result': 0,
                                              'violations': {}})
            group['cases'] += 1
            if model == 'torn':
                tore = position < len(events) and events[position].kind == 'write'
                group['tore'] = group.get('tore', 0) + tore
            if result is None:
                group['no_result'] += 1
                self.fail(record, 'the verifier gave no result', image)
                continue
            found = violations(result)
            if not found:
                group['passed'] += 1
                continue
            for invariant in found:
                group['violations'][invariant] = group['violations'].get(invariant, 0) + 1
            if workload == 'W1' and result['data'] == 0:
                intact = self.acknowledged_intact(image, record)
                record['acknowledged_intact'] = intact
                key = 'I2_acknowledged_intact' if intact else 'I2_acknowledged_lost'
                group[key] = group.get(key, 0) + 1
            failed.append(dict(record, result=result))
        return groups, failed

    def acknowledged_intact(self, image, record):
        """Whether every acknowledged W1 record is intact, so that only a later step is stale."""
        result, reason = self.verify(record['exe'], image, 'W1', record['acked'], acked_only=True)
        if result is None:
            self.fail(record, f'the acked-only verifier gave no result: {reason}', image)
            return False
        return result['data'] == 1

    def flush_control(self):
        """fat-nofix, cached, lost: W1 must lose acknowledged data after each mark."""
        events = self.trace('nofix', 'W1', 'cached')
        cases = []
        failed = False
        marks = [index + 1 for index, event in enumerate(events) if event.kind == 'mark']
        for position in marks:
            record, image, result = self.case('nofix', events, 'W1', 'cached', 'lost', position)
            ok = result is not None and result['mount'] == 1 and result['data'] == 0
            cases.append(dict(record, result=result, failed_as_required=ok))
            if not ok:
                failed = True
                self.fail(record, 'the flush control did not report data=0 with mount=1', image)
        return {
            'name': 'flush', 'image': 'fat-nofix', 'profile': 'cached', 'model': 'lost',
            'workload': 'W1', 'required': 'mount=1 and data=0 at every position after a mark',
            'flushes_in_trace': sum(1 for event in events if event.kind == 'flush'),
            'cases': cases, 'failed_as_required': bool(marks) and not failed,
        }

    def early_ack_observation(self):
        """fat-fix, cached, lost, W1 with each mark before its sync: recorded, not asserted."""
        events = self.trace('fix', 'W1', 'cached', phase='run-early')
        cases = []
        for position in [index + 1 for index, event in enumerate(events) if event.kind == 'mark']:
            record, image, result = self.case('fix', events, 'W1', 'cached', 'lost', position)
            record['phase'] = 'run-early'
            if result is None:
                self.fail(record, 'the verifier gave no result', image)
            cases.append(dict(record, result=result))
        return {
            'image': 'fat-fix', 'phase': 'run-early', 'profile': 'cached', 'model': 'lost',
            'workload': 'W1', 'positions': 'after each mark', 'cases': cases,
            'data_failures': sum(1 for case in cases
                                 if case['result'] is not None and case['result']['data'] == 0),
        }

    def damaged_control(self):
        """fat-fix, the baseline with its boot sector overwritten, must not mount."""
        boot = self.baseline[:SECTOR]
        if boot[510:512] != b'\x55\xaa' or boot[0] not in (0xeb, 0xe9):
            raise RunError('sector 0 of the baseline is not a FAT boot sector')
        image = b'\xa5' * SECTOR + self.baseline[SECTOR:]
        result, reason = self.verify('fix', image, 'W1', 0)
        ok = result is not None and result['mount'] == 0
        record = {'exe': 'fix', 'workload': 'W1', 'acked': 0, 'damage': 'sector 0 = 0xa5'}
        if result is None:
            record['no_result'] = reason
        if not ok:
            self.fail(record, 'the damaged image control did not report mount=0', image)
        return {'name': 'damaged image', 'image': 'fat-fix', 'required': 'mount=0',
                'case': dict(record, result=result), 'failed_as_required': ok}

    def execute(self):
        started = time.monotonic()
        self.provision()
        report = {'fs': self.args.fs, 'seed': self.args.seed, 'samples': self.args.samples,
                  'medium_bytes': MEDIUM, 'asserted': [], 'traces': {}, 'sanity': [],
                  'observations': {}, 'observed_failures': [], 'controls': []}
        for workload in WORKLOADS:
            for profile in PROFILES:
                events = self.trace('fix', workload, profile)
                report['traces'].setdefault(workload, {})[profile] = {
                    'events': len(events),
                    'writes': sum(1 for e in events if e.kind == 'write'),
                    'flushes': sum(1 for e in events if e.kind == 'flush'),
                    'marks': sum(1 for e in events if e.kind == 'mark'),
                    'flush_positions': len(mandatory_positions(events)),
                    'metaroot_positions': len(self.metaroot_positions(events)),
                }
                self.sanity(events, workload, profile, report['sanity'])
                groups, failed = self.observe(events, workload, profile)
                report['observations'].setdefault(workload, {})[profile] = groups
                report['observed_failures'] += failed
        report['controls'] = [self.flush_control(), self.damaged_control()]
        report['early_ack_observation'] = self.early_ack_observation()
        seconds = time.monotonic() - started
        if seconds > self.args.max_seconds:
            self.failures.append({'reason': f'took {seconds:.1f} s, over --max-seconds '
                                            f'{self.args.max_seconds}'})
        report['counts'] = {'cases': self.cases, 'processes': self.processes,
                            'observed_failures': len(report['observed_failures'])}
        report['failures'] = self.failures
        report['timing'] = {'seconds': round(seconds, 1), 'max_seconds': self.args.max_seconds,
                            'machine': platform.machine()}
        return report


def summarise(report):
    for workload, profiles in report['observations'].items():
        for profile, groups in profiles.items():
            for model, group in groups.items():
                found = ' '.join(f'{k}={v}' for k, v in sorted(group['violations'].items()))
                for key in ('tore', 'I2_acknowledged_intact', 'I2_acknowledged_lost'):
                    if key in group:
                        found += f' {key}={group[key]}'
                print(f'{workload} {profile:7} {model:9} cases {group["cases"]:4} '
                      f'passed {group["passed"]:4} {found}')
    early = report['early_ack_observation']
    print(f'run-early W1 lost: {len(early["cases"])} cases, data=0 in {early["data_failures"]}')
    for control in report['controls']:
        print(f'control {control["name"]}: '
              f'{"failed as required" if control["failed_as_required"] else "DID NOT FAIL"}')
    counts = report['counts']
    print(f'{counts["cases"]} cases, {counts["processes"]} verifier processes, '
          f'{report["timing"]["seconds"]} s, seed {report["seed"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe-fix', required=True, help='image built with the flush correction')
    parser.add_argument('--exe-nofix', required=True, help='image built without it')
    parser.add_argument('--fs', required=True, choices=('fatfs', 'reliance'))
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--samples', type=int, required=True,
                        help='sampled positions per workload, profile and model')
    parser.add_argument('--max-seconds', type=float, required=True)
    parser.add_argument('--out', required=True, help='directory for the JSON and kept images')
    args = parser.parse_args()
    if args.fs == 'reliance':
        parser.error('--fs reliance needs the Reliance Edge images and metaroot recognition, '
                     'which a later task adds')
    if args.samples < 0:
        parser.error('--samples must not be negative')

    os.makedirs(args.out, exist_ok=True)
    replay = Replay(args)
    try:
        report = replay.execute()
    except RunError as error:
        print(f'FAIL {error}', file=sys.stderr)
        report = {'fs': args.fs, 'seed': args.seed, 'samples': args.samples,
                  'error': str(error), 'failures': replay.failures}
    with open(os.path.join(args.out, f'{args.fs}.json'), 'w') as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write('\n')
    if 'error' in report:
        return 1
    summarise(report)
    bad_controls = [c['name'] for c in report['controls'] if not c['failed_as_required']]
    if report['failures'] or bad_controls:
        print(f'FAIL: {len(report["failures"])} failures; controls that did not fail: '
              f'{bad_controls}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
